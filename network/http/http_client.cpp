#include "http_client.h"
#include "logging.h"
#include <libwebsockets.h>
#include <chrono>
#include <condition_variable>

static const char *TAG = "net.http";

HttpClient::HttpClient():
    context_(nullptr),
    running_(false),
    next_request_id_(1),
    default_timeout_ms_(30000),
    default_verify_ssl_(true)
{
}

HttpClient::~HttpClient()
{
    cleanup();
}

NetworkResult HttpClient::init()
{
    std::lock_guard<std::mutex> lock(requests_mutex_);

    if (context_ != nullptr)
    {
        ESP_LOGE(TAG, "HTTP client already initialized");
        return NetworkResult::ALREADY_CONNECTED;
    }

    return createContext();
}

void HttpClient::cleanup()
{
    if (running_.load())
    {
        running_.store(false);

        if (service_thread_.joinable())
        {
            service_thread_.join();
        }
    }

    destroyContext();

    std::lock_guard<std::mutex> lock(requests_mutex_);
    while (!pending_requests_.empty())
    {
        pending_requests_.pop();
    }
    active_requests_.clear();
}

NetworkResult HttpClient::createContext()
{
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = nullptr;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context_ = lws_create_context(&info);
    if (!context_)
    {
        ESP_LOGE(TAG, "Failed to create libwebsockets context");
        return NetworkResult::ERROR;
    }

    running_.store(true);
    service_thread_ = std::thread(&HttpClient::serviceThread, this);

    ESP_LOGI(TAG, "HTTP client initialized successfully");
    return NetworkResult::OK;
}

void HttpClient::destroyContext()
{
    if (context_)
    {
        lws_context_destroy(context_);
        context_ = nullptr;
        ESP_LOGI(TAG, "HTTP client context destroyed");
    }
}

std::string HttpClient::methodToString(HttpMethod method) const
{
    switch (method)
    {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default: return "GET";
    }
}

HttpStatus HttpClient::intToHttpStatus(int status_code) const
{
    switch (status_code)
    {
        case 200: return HttpStatus::OK;
        case 201: return HttpStatus::CREATED;
        case 204: return HttpStatus::NO_CONTENT;
        case 400: return HttpStatus::BAD_REQUEST;
        case 401: return HttpStatus::UNAUTHORIZED;
        case 403: return HttpStatus::FORBIDDEN;
        case 404: return HttpStatus::NOT_FOUND;
        case 500: return HttpStatus::INTERNAL_SERVER_ERROR;
        case 502: return HttpStatus::BAD_GATEWAY;
        case 503: return HttpStatus::SERVICE_UNAVAILABLE;
        default: return HttpStatus::UNKNOWN;
    }
}

NetworkResult HttpClient::sendRequest(const HttpRequest& request, HttpResponseCallback callback)
{
    if (!running_.load())
    {
        ESP_LOGE(TAG, "HTTP client not initialized");
        return NetworkResult::NOT_INITIALIZED;
    }

    if (!callback)
    {
        ESP_LOGE(TAG, "Invalid callback provided");
        return NetworkResult::INVALID_PARAMETER;
    }

    if (request.url.empty())
    {
        ESP_LOGE(TAG, "Empty URL provided");
        return NetworkResult::INVALID_PARAMETER;
    }

    auto internal_request = std::make_shared<HttpRequestInternal>();
    internal_request->request = request;
    internal_request->callback = callback;
    internal_request->response = std::make_shared<HttpResponse>();
    internal_request->request_id = next_request_id_++;

    if (internal_request->request.timeout_ms == 0)
    {
        internal_request->request.timeout_ms = default_timeout_ms_;
    }

    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        pending_requests_.push(internal_request);
    }

    ESP_LOGD(TAG, "Queued HTTP %s request to %s (ID: %u)",
              methodToString(request.method).c_str(), request.url.c_str(),
              internal_request->request_id);

    return NetworkResult::OK;
}

NetworkResult HttpClient::sendRequestSync(const HttpRequest& request, HttpResponse& response)
{
    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool completed = false;
    NetworkResult result = NetworkResult::OK;

    auto callback = [&](const HttpResponse& resp)
    {
        std::lock_guard<std::mutex> lock(sync_mutex);
        response = resp;
        completed = true;
        sync_cv.notify_one();
    };

    NetworkResult send_result = sendRequest(request, callback);
    if (send_result != NetworkResult::OK)
    {
        return send_result;
    }

    std::unique_lock<std::mutex> lock(sync_mutex);
    auto timeout = std::chrono::milliseconds(request.timeout_ms > 0 ? request.timeout_ms : default_timeout_ms_);

    if (!sync_cv.wait_for(lock, timeout, [&completed] { return completed; }))
    {
        ESP_LOGW(TAG, "HTTP synchronous request timed out");
        result = NetworkResult::TIMEOUT;
    }

    return result;
}

void HttpClient::cancelAllRequests()
{
    std::lock_guard<std::mutex> lock(requests_mutex_);

    size_t cancelled_pending = pending_requests_.size();
    while (!pending_requests_.empty())
    {
        pending_requests_.pop();
    }

    for (auto& [wsi, request] : active_requests_)
    {
        if (request->callback && !request->completed)
        {
            request->response->error_message = "Request cancelled";
            request->callback(*request->response);
            request->completed = true;
        }
    }
    active_requests_.clear();

    ESP_LOGI(TAG, "Cancelled %zu pending HTTP requests and %zu active requests",
             cancelled_pending, active_requests_.size());
}

size_t HttpClient::getPendingRequestCount() const
{
    std::lock_guard<std::mutex> lock(requests_mutex_);
    return pending_requests_.size() + active_requests_.size();
}

void HttpClient::setDefaultTimeout(uint32_t timeoutMs)
{
    default_timeout_ms_ = timeoutMs;
}

void HttpClient::setDefaultVerifySsl(bool verify)
{
    default_verify_ssl_ = verify;
}

void HttpClient::setErrorCallback(NetworkErrorCallback callback)
{
    error_callback_ = callback;
}

void HttpClient::serviceThread()
{
    ESP_LOGD(TAG, "HTTP client service thread started");

    while (running_.load())
    {
        std::shared_ptr<HttpRequestInternal> request;

        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            if (!pending_requests_.empty())
            {
                request = pending_requests_.front();
                pending_requests_.pop();
            }
        }

        if (request)
        {
            struct lws_client_connect_info connect_info;
            memset(&connect_info, 0, sizeof(connect_info));

            std::string url = request->request.url;
            std::string protocol, host, path;
            int port = 80;
            bool use_ssl = false;

            if (url.find("https://") == 0)
            {
                use_ssl = true;
                port = 443;
                url = url.substr(8);
            }
            else if (url.find("http://") == 0)
            {
                url = url.substr(7);
            }

            size_t path_pos = url.find('/');
            if (path_pos != std::string::npos)
            {
                host = url.substr(0, path_pos);
                path = url.substr(path_pos);
            }
            else
            {
                host = url;
                path = "/";
            }

            size_t port_pos = host.find(':');
            if (port_pos != std::string::npos)
            {
                port = std::stoi(host.substr(port_pos + 1));
                host = host.substr(0, port_pos);
            }

            connect_info.context = context_;
            connect_info.address = host.c_str();
            connect_info.port = port;
            connect_info.path = path.c_str();
            connect_info.host = host.c_str();
            connect_info.origin = host.c_str();
            connect_info.method = methodToString(request->request.method).c_str();
            connect_info.userdata = request.get();

            if (use_ssl)
            {
                connect_info.ssl_connection = LCCSCF_USE_SSL;
                if (!request->request.verify_ssl)
                {
                    connect_info.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED |
                                                   LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                                                   LCCSCF_ALLOW_INSECURE;
                }
            }

            struct lws* wsi = lws_client_connect_via_info(&connect_info);
            if (!wsi)
            {
                ESP_LOGE(TAG, "Failed to create HTTP client connection");
                request->response->error_message = "Failed to create connection";
                if (request->callback)
                {
                    request->callback(*request->response);
                }
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(requests_mutex_);
                active_requests_[wsi] = request;
            }

            ESP_LOGD(TAG, "Started HTTP %s request to %s:%d%s (ID: %u)",
                     methodToString(request->request.method).c_str(),
                     host.c_str(), port, path.c_str(), request->request_id);
        }

        if (context_)
        {
            lws_service(context_, 50);
        }
    }

    ESP_LOGD(TAG, "HTTP client service thread terminated");
}

int HttpClient::httpCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
    HttpRequestInternal* request = static_cast<HttpRequestInternal*>(user);

    if (!request || !request->response)
    {
        return -1;
    }

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            const char* error_msg = in ? static_cast<const char*>(in) : "Connection error";
            ESP_LOGE(TAG, "HTTP client connection error: %s", error_msg);
            request->response->error_message = error_msg;
            if (request->callback && !request->completed)
            {
                request->callback(*request->response);
                request->completed = true;
            }
            return -1;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            ESP_LOGD(TAG, "HTTP client connection established");
            return 0;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            const char* data = static_cast<const char*>(in);
            request->response->body.data.insert(request->response->body.data.end(),
                                               data, data + len);
            request->response->body.size = request->response->body.data.size();
            return 0;
        }

        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
        {
            if (!request->request.body.data.empty())
            {
                unsigned char buffer[LWS_PRE + 4096];
                size_t body_size = request->request.body.size;

                if (body_size > 4096)
                {
                    body_size = 4096;
                }

                memcpy(&buffer[LWS_PRE], request->request.body.data.data(), body_size);

                int write_result = lws_write(wsi, &buffer[LWS_PRE], body_size,
                                           LWS_WRITE_HTTP_FINAL);

                if (write_result < 0)
                {
                    ESP_LOGE(TAG, "Failed to write HTTP request body");
                    return -1;
                }

                request->request.body.data.erase(request->request.body.data.begin(),
                                                request->request.body.data.begin() + body_size);
                request->request.body.size = request->request.body.data.size();

                if (!request->request.body.data.empty())
                {
                    lws_callback_on_writable(wsi);
                }
            }
            return 0;
        }

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
        {
            int status = lws_http_client_http_response(wsi);
            request->response->status_code = static_cast<HttpStatus>(status);

            ESP_LOGD(TAG, "HTTP request completed with status %d", status);

            if (request->callback && !request->completed)
            {
                request->callback(*request->response);
                request->completed = true;
            }
            return -1;
        }

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
        {
            ESP_LOGD(TAG, "HTTP client connection closed");
            return -1;
        }

        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}
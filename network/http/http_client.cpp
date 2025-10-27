#include "http_client.h"
#include "logging.h"
#include "mongoose.h"
#include <chrono>
#include <condition_variable>

static const char *TAG = "net.http";

HttpClient::HttpClient():
    mgr_(nullptr),
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
    if (mgr_ != nullptr)
    {
        ESP_LOGW(TAG, "HTTP client already initialized");
        return NetworkResult::OK;
    }

    mgr_ = new mg_mgr();
    if (!mgr_)
    {
        ESP_LOGE(TAG, "Failed to allocate mongoose manager");
        return NetworkResult::ERROR;
    }

    mg_mgr_init(mgr_);
    mgr_->userdata = this;

    running_.store(true);
    service_thread_ = std::thread(&HttpClient::serviceThread, this);

    ESP_LOGI(TAG, "HTTP client initialized successfully");
    return NetworkResult::OK;
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

    destroyManager();

    std::lock_guard<std::mutex> lock(requests_mutex_);
    while (!pending_requests_.empty())
    {
        pending_requests_.pop();
    }
    active_requests_.clear();
}

void HttpClient::destroyManager()
{
    if (mgr_)
    {
        mg_mgr_free(mgr_);
        delete mgr_;
        mgr_ = nullptr;
        ESP_LOGI(TAG, "HTTP client manager destroyed");
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

    for (auto& [conn, request] : active_requests_)
    {
        if (request->callback && !request->completed)
        {
            request->response->error_message = "Request cancelled";
            request->callback(*request->response);
            request->completed = true;
        }
        if (conn)
        {
            conn->is_closing = 1;
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
        bool has_work = false;

        // Check for new pending requests
        std::shared_ptr<HttpRequestInternal> request;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            if (!pending_requests_.empty())
            {
                request = pending_requests_.front();
                pending_requests_.pop();
            }

            has_work = !active_requests_.empty();
        }

        // Process the request outside the mutex
        if (request && mgr_)
        {
            has_work = true;

            // Set timeout
            request->timeout_time = mg_millis() + request->request.timeout_ms;

            // Create connection
            struct mg_connection* c = mg_http_connect(mgr_, request->request.url.c_str(),
                                                      eventHandler, request.get());

            if (!c)
            {
                ESP_LOGE(TAG, "Failed to create HTTP connection to %s", request->request.url.c_str());
                request->response->error_message = "Failed to create connection";
                if (request->callback)
                {
                    request->callback(*request->response);
                }
            }
            else
            {
                request->connection = c;

                {
                    std::lock_guard<std::mutex> lock(requests_mutex_);
                    active_requests_[c] = request;
                }

                ESP_LOGI(TAG, "Started HTTP %s request to %s (ID: %u)",
                         methodToString(request->request.method).c_str(),
                         request->request.url.c_str(), request->request_id);
            }
        }

        // Check for timeouts
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            uint64_t now = mg_millis();

            for (auto it = active_requests_.begin(); it != active_requests_.end();)
            {
                auto& req = it->second;
                if (req->timeout_time > 0 && now > req->timeout_time &&
                    (it->first->is_connecting || it->first->is_resolving))
                {
                    ESP_LOGW(TAG, "Request ID %u timed out", req->request_id);
                    req->response->error_message = "Request timeout";
                    if (req->callback && !req->completed)
                    {
                        req->callback(*req->response);
                        req->completed = true;
                    }
                    it->first->is_closing = 1;
                    it = active_requests_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // Service the manager to process network events
        if (has_work && mgr_)
        {
            mg_mgr_poll(mgr_, 50);
        }
        else
        {
            // No work to do, sleep longer to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ESP_LOGD(TAG, "HTTP client service thread terminated");
}


void HttpClient::eventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data)
{
    HttpRequestInternal* request = static_cast<HttpRequestInternal*>(fn_data);

    if (!request)
    {
        ESP_LOGW(TAG, "Request is null in event handler");
        return;
    }

    HttpClient* client = static_cast<HttpClient*>(c->mgr->userdata);

    switch (ev)
    {
        case MG_EV_OPEN:
        {
            ESP_LOGD(TAG, "Connection opened for request ID: %u", request->request_id);
            break;
        }

        case MG_EV_CONNECT:
        {
            ESP_LOGI(TAG, "Connection established for request ID: %u", request->request_id);

            // Build HTTP headers
            std::string extra_headers;
            for (const auto& header : request->request.headers)
            {
                extra_headers += header.first + ": " + header.second + "\r\n";
            }

            // Send HTTP request
            const char* method = nullptr;
            switch (request->request.method)
            {
                case HttpMethod::GET: method = "GET"; break;
                case HttpMethod::POST: method = "POST"; break;
                case HttpMethod::PUT: method = "PUT"; break;
                case HttpMethod::DELETE: method = "DELETE"; break;
                case HttpMethod::HEAD: method = "HEAD"; break;
                case HttpMethod::OPTIONS: method = "OPTIONS"; break;
                default: method = "GET"; break;
            }

            // Extract URI from URL
            struct mg_str host = mg_url_host(request->request.url.c_str());
            const char* uri = mg_url_uri(request->request.url.c_str());
            if (!uri || uri[0] == '\0')
            {
                uri = "/";
            }

            // Build complete HTTP request
            std::string http_request;
            http_request.reserve(512); // Pre-allocate to avoid reallocs

            // Request line
            http_request += method;
            http_request += " ";
            http_request += uri;
            http_request += " HTTP/1.1\r\n";

            // Host header
            http_request += "Host: ";
            http_request.append(host.ptr, host.len);
            http_request += "\r\n";

            // Content-Length if body present
            if (!request->request.body.data.empty())
            {
                http_request += "Content-Length: ";
                http_request += std::to_string(request->request.body.size);
                http_request += "\r\n";
            }

            // Extra headers
            http_request += extra_headers;

            // Empty line to end headers
            http_request += "\r\n";

            // Send headers
            mg_send(c, http_request.data(), http_request.size());

            // Send body if present
            if (!request->request.body.data.empty())
            {
                mg_send(c, request->request.body.data.data(), request->request.body.size);
            }
            break;
        }

        case MG_EV_HTTP_MSG:
        {
            struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);

            ESP_LOGI(TAG, "HTTP response received for request ID: %u", request->request_id);

            // Parse status code
            int status_code = mg_http_status(hm);
            request->response->status_code = client->intToHttpStatus(status_code);

            ESP_LOGI(TAG, "HTTP status: %d", status_code);

            // Copy response body
            if (hm->body.len > 0)
            {
                request->response->body.data.assign(hm->body.ptr, hm->body.ptr + hm->body.len);
                request->response->body.size = hm->body.len;
                ESP_LOGD(TAG, "Received body: %zu bytes", hm->body.len);
            }

            // Copy response headers
            for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->headers[i].name.len > 0; i++)
            {
                std::string name(hm->headers[i].name.ptr, hm->headers[i].name.len);
                std::string value(hm->headers[i].value.ptr, hm->headers[i].value.len);
                request->response->headers[name] = value;
            }

            // Call callback
            if (request->callback && !request->completed)
            {
                request->callback(*request->response);
                request->completed = true;
            }

            // Remove from active requests
            if (client)
            {
                std::lock_guard<std::mutex> lock(client->requests_mutex_);
                client->active_requests_.erase(c);
            }

            c->is_closing = 1;
            break;
        }

        case MG_EV_ERROR:
        {
            const char* error_msg = static_cast<const char*>(ev_data);
            ESP_LOGE(TAG, "HTTP error for request ID %u: %s", request->request_id, error_msg);

            request->response->error_message = error_msg;

            if (request->callback && !request->completed)
            {
                request->callback(*request->response);
                request->completed = true;
            }

            // Remove from active requests
            if (client)
            {
                std::lock_guard<std::mutex> lock(client->requests_mutex_);
                client->active_requests_.erase(c);
            }
            break;
        }

        case MG_EV_CLOSE:
        {
            ESP_LOGD(TAG, "Connection closed for request ID: %u", request->request_id);

            // If not completed yet, it's an unexpected close
            if (!request->completed)
            {
                request->response->error_message = "Connection closed unexpectedly";
                if (request->callback)
                {
                    request->callback(*request->response);
                    request->completed = true;
                }
            }

            // Remove from active requests
            if (client)
            {
                std::lock_guard<std::mutex> lock(client->requests_mutex_);
                client->active_requests_.erase(c);
            }
            break;
        }

        default:
            break;
    }
}

std::shared_ptr<HttpRequestInternal> HttpClient::findRequestForConnection(struct mg_connection* c)
{
    std::unique_lock<std::mutex> lock(requests_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return nullptr;
    }

    auto it = active_requests_.find(c);
    if (it != active_requests_.end())
    {
        return it->second;
    }
    return nullptr;
}



#include "websocket_client.h"
#include "logging.h"
#include <libwebsockets.h>
#include <chrono>
#include <cstring>

#ifdef __linux__
#include <unistd.h>
#elif defined(ESP_PLATFORM)
#include "esp_timer.h"
#endif

static const char *TAG = "net.ws";

static uint64_t getCurrentTimestamp()
{
#ifdef __linux__
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#elif defined(ESP_PLATFORM)
    return esp_timer_get_time() / 1000;
#endif
}

// Forward declaration for the callback
static int websocket_callback_wrapper(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static struct lws_protocols protocols[] =
{
    {
        "calaos-websocket-protocol",
        websocket_callback_wrapper,
        0,
        4096,
        0, nullptr, 0
    },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

WebSocketClient::WebSocketClient():
    context_(nullptr),
    wsi_(nullptr),
    protocols_(protocols),
    state_(WebSocketState::DISCONNECTED),
    running_(false),
    should_reconnect_(false),
    reconnect_attempts_(0),
    last_ping_time_(0),
    last_pong_time_(0)
{
}

WebSocketClient::~WebSocketClient()
{
    cleanup();
}

NetworkResult WebSocketClient::init()
{
    std::lock_guard<std::mutex> lock(config_mutex_);

    if (context_ != nullptr)
    {
        ESP_LOGE(TAG, "WebSocket client already initialized");
        return NetworkResult::ALREADY_CONNECTED;
    }

    return createContext();
}

void WebSocketClient::cleanup()
{
    disconnect();

    if (running_.load())
    {
        running_.store(false);

        if (service_thread_.joinable())
        {
            service_thread_.join();
        }

        if (reconnect_thread_.joinable())
        {
            reconnect_thread_.join();
        }
    }

    destroyContext();

    std::lock_guard<std::mutex> lock(messages_mutex_);
    while (!outgoing_messages_.empty())
    {
        outgoing_messages_.pop();
    }
}

NetworkResult WebSocketClient::createContext()
{
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = this;

    context_ = lws_create_context(&info);
    if (!context_)
    {
        ESP_LOGE(TAG, "Failed to create WebSocket context");
        return NetworkResult::ERROR;
    }

    running_.store(true);
    service_thread_ = std::thread(&WebSocketClient::serviceThread, this);
    reconnect_thread_ = std::thread(&WebSocketClient::reconnectThread, this);

    ESP_LOGI(TAG, "WebSocket client initialized successfully");
    return NetworkResult::OK;
}

void WebSocketClient::destroyContext()
{
    if (context_)
    {
        lws_context_destroy(context_);
        context_ = nullptr;
        ESP_LOGI(TAG, "WebSocket client context destroyed");
    }
}

NetworkResult WebSocketClient::connect(const WebSocketConfig& config)
{
    if (!running_.load())
    {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return NetworkResult::NOT_INITIALIZED;
    }

    if (config.url.empty())
    {
        ESP_LOGE(TAG, "Empty WebSocket URL provided");
        return NetworkResult::INVALID_PARAMETER;
    }

    if (state_.load() == WebSocketState::CONNECTED ||
        state_.load() == WebSocketState::CONNECTING)
    {
        ESP_LOGW(TAG, "WebSocket already connected or connecting");
        return NetworkResult::ALREADY_CONNECTED;
    }

    std::lock_guard<std::mutex> lock(config_mutex_);
    current_config_ = config;
    reconnect_attempts_ = 0;

    state_.store(WebSocketState::CONNECTING);
    if (state_callback_)
    {
        state_callback_(WebSocketState::CONNECTING);
    }

    std::string url = current_config_.url;
    std::string protocol, host, path;
    int port = 80;
    bool use_ssl = false;

    if (url.find("wss://") == 0)
    {
        use_ssl = true;
        port = 443;
        url = url.substr(6);
    }
    else if (url.find("ws://") == 0)
    {
        url = url.substr(5);
    }
    else
    {
        ESP_LOGE(TAG, "Invalid WebSocket URL scheme");
        state_.store(WebSocketState::ERROR);
        return NetworkResult::INVALID_PARAMETER;
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

    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));

    connect_info.context = context_;
    connect_info.address = host.c_str();
    connect_info.port = port;
    connect_info.path = path.c_str();
    connect_info.host = host.c_str();
    connect_info.origin = host.c_str();
    connect_info.protocol = !current_config_.protocols.empty() ?
                           current_config_.protocols[0].c_str() : protocols[0].name;
    connect_info.userdata = this;

    if (use_ssl)
    {
        connect_info.ssl_connection = LCCSCF_USE_SSL;
        if (!current_config_.verify_ssl)
        {
            connect_info.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED |
                                          LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                                          LCCSCF_ALLOW_INSECURE;
        }
    }

    wsi_ = lws_client_connect_via_info(&connect_info);
    if (!wsi_)
    {
        ESP_LOGE(TAG, "Failed to create WebSocket connection");
        state_.store(WebSocketState::ERROR);
        if (error_callback_)
        {
            error_callback_(NetworkResult::CONNECTION_FAILED, "Failed to create connection");
        }
        return NetworkResult::CONNECTION_FAILED;
    }

    ESP_LOGI(TAG, "Connecting to WebSocket: %s", current_config_.url.c_str());
    return NetworkResult::OK;
}

void WebSocketClient::disconnect()
{
    should_reconnect_.store(false);

    if (wsi_)
    {
        state_.store(WebSocketState::CLOSING);
        lws_close_reason(wsi_, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
        wsi_ = nullptr;
    }

    state_.store(WebSocketState::DISCONNECTED);
    if (state_callback_)
    {
        state_callback_(WebSocketState::DISCONNECTED);
    }

    ESP_LOGI(TAG, "WebSocket disconnected");
}

NetworkResult WebSocketClient::sendText(const std::string& message)
{
    if (state_.load() != WebSocketState::CONNECTED)
    {
        ESP_LOGW(TAG, "WebSocket not connected, cannot send text message");
        return NetworkResult::NOT_CONNECTED;
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);
    outgoing_messages_.emplace(message);

    if (wsi_)
    {
        lws_callback_on_writable(wsi_);
    }

    ESP_LOGD(TAG, "Queued text message for WebSocket send (%zu bytes)", message.size());
    return NetworkResult::OK;
}

NetworkResult WebSocketClient::sendBinary(const std::vector<uint8_t>& data)
{
    if (state_.load() != WebSocketState::CONNECTED)
    {
        ESP_LOGW(TAG, "WebSocket not connected, cannot send binary message");
        return NetworkResult::NOT_CONNECTED;
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);
    outgoing_messages_.emplace(data);

    if (wsi_)
    {
        lws_callback_on_writable(wsi_);
    }

    ESP_LOGD(TAG, "Queued binary message for WebSocket send (%zu bytes)", data.size());
    return NetworkResult::OK;
}

NetworkResult WebSocketClient::sendJson(const std::string& json)
{
    return sendText(json);
}

NetworkResult WebSocketClient::ping(const std::string& data)
{
    if (state_.load() != WebSocketState::CONNECTED)
    {
        ESP_LOGW(TAG, "WebSocket not connected, cannot send ping");
        return NetworkResult::NOT_CONNECTED;
    }

    if (wsi_)
    {
        unsigned char ping_data[LWS_PRE + 125];
        size_t ping_len = std::min(data.size(), size_t(125));

        if (ping_len > 0)
        {
            memcpy(&ping_data[LWS_PRE], data.data(), ping_len);
        }

        int result = lws_write(wsi_, &ping_data[LWS_PRE], ping_len, LWS_WRITE_PING);
        if (result < 0)
        {
            ESP_LOGE(TAG, "Failed to send WebSocket ping");
            return NetworkResult::ERROR;
        }

        last_ping_time_ = getCurrentTimestamp();
        ESP_LOGD(TAG, "Sent WebSocket ping (%zu bytes)", ping_len);
    }

    return NetworkResult::OK;
}

WebSocketState WebSocketClient::getState() const
{
    return state_.load();
}

bool WebSocketClient::isConnected() const
{
    return state_.load() == WebSocketState::CONNECTED;
}

void WebSocketClient::setMessageCallback(WebSocketMessageCallback callback)
{
    message_callback_ = callback;
}

void WebSocketClient::setStateCallback(WebSocketStateCallback callback)
{
    state_callback_ = callback;
}

void WebSocketClient::setCloseCallback(WebSocketCloseCallback callback)
{
    close_callback_ = callback;
}

void WebSocketClient::setErrorCallback(NetworkErrorCallback callback)
{
    error_callback_ = callback;
}

void WebSocketClient::scheduleReconnect()
{
    if (current_config_.auto_reconnect &&
        reconnect_attempts_ < current_config_.max_reconnect_attempts)
    {
        should_reconnect_.store(true);
        ESP_LOGI(TAG, "Scheduling WebSocket reconnection attempt %u/%u in %u ms",
                 reconnect_attempts_ + 1, current_config_.max_reconnect_attempts,
                 current_config_.reconnect_delay_ms);
    }
    else
    {
        ESP_LOGW(TAG, "WebSocket reconnection disabled or maximum attempts reached");
    }
}

void WebSocketClient::processOutgoingMessages()
{
    std::lock_guard<std::mutex> lock(messages_mutex_);

    if (outgoing_messages_.empty() || !wsi_)
    {
        return;
    }

    WebSocketMessage& message = outgoing_messages_.front();

    unsigned char buffer[LWS_PRE + 4096];
    size_t message_size = std::min(message.data.size(), size_t(4096));

    memcpy(&buffer[LWS_PRE], message.data.data(), message_size);

    enum lws_write_protocol write_mode = message.is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;
    if (message.data.size() <= 4096)
    {
        write_mode = static_cast<enum lws_write_protocol>(write_mode | LWS_WRITE_HTTP_FINAL);
    }

    int result = lws_write(wsi_, &buffer[LWS_PRE], message_size, write_mode);

    if (result < 0)
    {
        ESP_LOGE(TAG, "Failed to write WebSocket message");
        if (error_callback_)
        {
            error_callback_(NetworkResult::ERROR, "Failed to write message");
        }
        return;
    }

    message.data.erase(0, message_size);

    if (message.data.empty())
    {
        outgoing_messages_.pop();
        ESP_LOGD(TAG, "Sent WebSocket %s message (%d bytes)",
                 message.is_binary ? "binary" : "text", result);
    }

    if (!outgoing_messages_.empty())
    {
        lws_callback_on_writable(wsi_);
    }
}

void WebSocketClient::serviceThread()
{
    ESP_LOGD(TAG, "WebSocket client service thread started");

    while (running_.load())
    {
        if (context_)
        {
            lws_service(context_, 50);
        }

        if (state_.load() == WebSocketState::CONNECTED && wsi_)
        {
            uint64_t current_time = getCurrentTimestamp();

            if (current_config_.ping_interval_ms > 0 &&
                (current_time - last_ping_time_) > current_config_.ping_interval_ms)
            {
                ping();
            }

            if (current_config_.pong_timeout_ms > 0 && last_ping_time_ > 0 &&
                (current_time - last_ping_time_) > current_config_.pong_timeout_ms &&
                last_pong_time_ < last_ping_time_)
            {
                ESP_LOGW(TAG, "WebSocket pong timeout, disconnecting");
                disconnect();
                scheduleReconnect();
            }
        }
    }

    ESP_LOGD(TAG, "WebSocket client service thread terminated");
}

void WebSocketClient::reconnectThread()
{
    ESP_LOGD(TAG, "WebSocket reconnect thread started");

    while (running_.load())
    {
        if (should_reconnect_.load() &&
            state_.load() == WebSocketState::DISCONNECTED)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(current_config_.reconnect_delay_ms));

            if (should_reconnect_.load() && running_.load())
            {
                reconnect_attempts_++;
                ESP_LOGI(TAG, "Attempting WebSocket reconnection (%u/%u)",
                         reconnect_attempts_, current_config_.max_reconnect_attempts);

                NetworkResult result = connect(current_config_);
                if (result == NetworkResult::OK)
                {
                    should_reconnect_.store(false);
                }
                else if (reconnect_attempts_ >= current_config_.max_reconnect_attempts)
                {
                    should_reconnect_.store(false);
                    ESP_LOGE(TAG, "Maximum WebSocket reconnection attempts reached");
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    ESP_LOGD(TAG, "WebSocket reconnect thread terminated");
}

int WebSocketClient::websocketCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
    struct lws_context* context = lws_get_context(wsi);
    WebSocketClient* client = static_cast<WebSocketClient*>(lws_context_user(context));

    if (!client)
    {
        return -1;
    }

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            const char* error_msg = in ? static_cast<const char*>(in) : "Connection error";
            ESP_LOGE(TAG, "WebSocket connection error: %s", error_msg);
            client->state_.store(WebSocketState::ERROR);
            if (client->error_callback_)
            {
                client->error_callback_(NetworkResult::CONNECTION_FAILED, error_msg);
            }
            if (client->state_callback_)
            {
                client->state_callback_(WebSocketState::ERROR);
            }
            client->scheduleReconnect();
            return -1;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            ESP_LOGI(TAG, "WebSocket connection established");
            client->state_.store(WebSocketState::CONNECTED);
            client->reconnect_attempts_ = 0;
            client->last_ping_time_ = getCurrentTimestamp();
            client->last_pong_time_ = client->last_ping_time_;

            if (client->state_callback_)
            {
                client->state_callback_(WebSocketState::CONNECTED);
            }
            return 0;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            const char* data = static_cast<const char*>(in);
            bool is_binary = lws_frame_is_binary(wsi);

            WebSocketMessage message;
            message.data = std::string(data, len);
            message.is_binary = is_binary;

            if (client->message_callback_)
            {
                client->message_callback_(message);
            }

            ESP_LOGD(TAG, "Received WebSocket %s message (%zu bytes)",
                     is_binary ? "binary" : "text", len);
            return 0;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            client->processOutgoingMessages();
            return 0;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        {
            client->last_pong_time_ = getCurrentTimestamp();
            ESP_LOGD(TAG, "Received WebSocket pong");
            return 0;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            ESP_LOGI(TAG, "WebSocket connection closed");
            client->wsi_ = nullptr;
            client->state_.store(WebSocketState::DISCONNECTED);

            if (client->state_callback_)
            {
                client->state_callback_(WebSocketState::DISCONNECTED);
            }

            if (client->close_callback_)
            {
                client->close_callback_(WebSocketCloseReason::NORMAL_CLOSURE, "Connection closed");
            }

            client->scheduleReconnect();
            return -1;
        }

        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

// Wrapper function for the static protocol callback
static int websocket_callback_wrapper(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
    return WebSocketClient::websocketCallback(wsi, reason, user, in, len);
}
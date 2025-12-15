#include "websocket_client.h"
#include "logging.h"
#include "mongoose.h"
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

WebSocketClient::WebSocketClient():
    mgr_(nullptr),
    conn_(nullptr),
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

    if (mgr_ != nullptr)
    {
        ESP_LOGE(TAG, "WebSocket client already initialized");
        return NetworkResult::ALREADY_CONNECTED;
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
    service_thread_ = std::thread(&WebSocketClient::serviceThread, this);
    reconnect_thread_ = std::thread(&WebSocketClient::reconnectThread, this);

    ESP_LOGI(TAG, "WebSocket client initialized");
    return NetworkResult::OK;
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

    destroyManager();

    std::lock_guard<std::mutex> lock(messages_mutex_);
    while (!outgoing_messages_.empty())
    {
        outgoing_messages_.pop();
    }
}

void WebSocketClient::destroyManager()
{
    if (mgr_)
    {
        mg_mgr_free(mgr_);
        delete mgr_;
        mgr_ = nullptr;
        ESP_LOGI(TAG, "WebSocket manager destroyed");
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

    // Build headers string for mongoose
    std::string headerStr;
    for (const auto& header : current_config_.headers)
    {
        headerStr += header.first + ": " + header.second + "\r\n";
    }

    ESP_LOGD(TAG, "WebSocket headers: %s", headerStr.c_str());

    // Create WebSocket connection using mongoose with headers
    if (headerStr.empty())
    {
        conn_ = mg_ws_connect(mgr_, current_config_.url.c_str(), websocketEventHandler, this, nullptr);
    }
    else
    {
        conn_ = mg_ws_connect(mgr_, current_config_.url.c_str(), websocketEventHandler, this, "%s", headerStr.c_str());
    }

    if (!conn_)
    {
        ESP_LOGE(TAG, "Failed to create WebSocket connection");
        state_.store(WebSocketState::ERROR);
        if (error_callback_)
        {
            error_callback_(NetworkResult::CONNECTION_FAILED, "Failed to create connection");
        }
        return NetworkResult::CONNECTION_FAILED;
    }

    ESP_LOGI(TAG, "WebSocket connecting to %s", current_config_.url.c_str());
    return NetworkResult::OK;
}

void WebSocketClient::disconnect()
{
    WebSocketState current_state = state_.load();

    if (current_state == WebSocketState::DISCONNECTED)
    {
        return;
    }

    should_reconnect_.store(false);

    std::lock_guard<std::mutex> lock(config_mutex_);

    if (conn_)
    {
        conn_->is_closing = 1;
        conn_ = nullptr;
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
        ESP_LOGW(TAG, "WebSocket not connected, cannot send message");
        return NetworkResult::NOT_CONNECTED;
    }

    WebSocketMessage msg(message);
    msg.is_binary = false;

    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        outgoing_messages_.push(msg);
    }

    processOutgoingMessages();

    return NetworkResult::OK;
}

NetworkResult WebSocketClient::sendBinary(const std::vector<uint8_t>& data)
{
    if (state_.load() != WebSocketState::CONNECTED)
    {
        ESP_LOGW(TAG, "WebSocket not connected, cannot send data");
        return NetworkResult::NOT_CONNECTED;
    }

    WebSocketMessage msg(data);
    msg.is_binary = true;

    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        outgoing_messages_.push(msg);
    }

    processOutgoingMessages();

    return NetworkResult::OK;
}

NetworkResult WebSocketClient::sendJson(const std::string& json)
{
    return sendText(json);
}

NetworkResult WebSocketClient::ping(const std::string& data)
{
    if (state_.load() != WebSocketState::CONNECTED || !conn_)
    {
        ESP_LOGW(TAG, "WebSocket not connected, cannot send ping");
        return NetworkResult::NOT_CONNECTED;
    }

    // Send WebSocket ping frame
    mg_ws_send(conn_, data.c_str(), data.length(), WEBSOCKET_OP_PING);
    last_ping_time_ = getCurrentTimestamp();

    ESP_LOGD(TAG, "WebSocket ping sent");
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

void WebSocketClient::setReconnectConfigCallback(ReconnectConfigCallback callback)
{
    reconnect_config_callback_ = callback;
}

void WebSocketClient::scheduleReconnect()
{
    if (!current_config_.auto_reconnect || reconnect_attempts_ >= current_config_.max_reconnect_attempts)
    {
        ESP_LOGW(TAG, "WebSocket reconnection disabled or max attempts reached");
        state_.store(WebSocketState::ERROR);
        return;
    }

    uint32_t delay_ms = current_config_.reconnect_delay_ms * (1 << std::min<uint32_t>(reconnect_attempts_, 5u));
    ESP_LOGI(TAG, "WebSocket will reconnect in %u ms (attempt %u)", delay_ms, reconnect_attempts_ + 1);

    should_reconnect_.store(true);
    state_.store(WebSocketState::CLOSING);

    if (state_callback_)
    {
        state_callback_(WebSocketState::CLOSING);
    }
}

void WebSocketClient::processOutgoingMessages()
{
    if (!conn_ || state_.load() != WebSocketState::CONNECTED)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(messages_mutex_);

    while (!outgoing_messages_.empty())
    {
        WebSocketMessage& msg = outgoing_messages_.front();

        unsigned char op = msg.is_binary ? WEBSOCKET_OP_BINARY : WEBSOCKET_OP_TEXT;

        mg_ws_send(conn_, msg.data.c_str(), msg.data.size(), op);

        ESP_LOGD(TAG, "WebSocket message sent: %zu bytes", msg.data.size());

        outgoing_messages_.pop();
    }
}

void WebSocketClient::serviceThread()
{
    ESP_LOGD(TAG, "WebSocket service thread started");

    while (running_.load())
    {
        if (mgr_)
        {
            mg_mgr_poll(mgr_, 50);
        }

        // Process outgoing messages
        if (state_.load() == WebSocketState::CONNECTED)
        {
            processOutgoingMessages();

            // Send periodic pings if enabled
            if (current_config_.ping_interval_ms > 0)
            {
                uint64_t now = getCurrentTimestamp();
                if (now - last_ping_time_ >= current_config_.ping_interval_ms)
                {
                    ping();
                }
                else if (current_config_.pong_timeout_ms > 0 &&
                         last_ping_time_ > last_pong_time_ &&
                         now - last_ping_time_ >= current_config_.pong_timeout_ms)
                {
                    ESP_LOGE(TAG, "WebSocket ping timeout");
                    disconnect();
                    scheduleReconnect();
                }
            }
        }
    }

    ESP_LOGD(TAG, "WebSocket service thread terminated");
}

void WebSocketClient::reconnectThread()
{
    ESP_LOGD(TAG, "WebSocket reconnect thread started");

    while (running_.load())
    {
        if (should_reconnect_.load())
        {
            should_reconnect_.store(false);
            reconnect_attempts_++;

            uint32_t delay_ms = current_config_.reconnect_delay_ms *
                               (1 << std::min<uint32_t>(reconnect_attempts_ - 1, 5u));

            ESP_LOGI(TAG, "WebSocket reconnecting in %u ms (attempt %u/%u)",
                     delay_ms, reconnect_attempts_, current_config_.max_reconnect_attempts);

            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

            if (running_.load() && state_.load() != WebSocketState::CONNECTED)
            {
                WebSocketConfig config;

                // If a reconnect config callback is set, use it to get fresh config
                // This allows regenerating auth headers (nonce, timestamp, HMAC)
                if (reconnect_config_callback_)
                {
                    ESP_LOGD(TAG, "Getting fresh config from reconnect callback");
                    config = reconnect_config_callback_();
                }
                else
                {
                    std::lock_guard<std::mutex> lock(config_mutex_);
                    config = current_config_;
                }

                ESP_LOGI(TAG, "WebSocket attempting reconnection...");
                NetworkResult result = connect(config);

                if (result != NetworkResult::OK)
                {
                    scheduleReconnect();
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ESP_LOGD(TAG, "WebSocket reconnect thread terminated");
}

void WebSocketClient::websocketEventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data)
{
    WebSocketClient* client = static_cast<WebSocketClient*>(fn_data);

    if (!client)
    {
        return;
    }

    switch (ev)
    {
        case MG_EV_ERROR:
        {
            const char* error_msg = static_cast<const char*>(ev_data);
            ESP_LOGE(TAG, "WebSocket error: %s", error_msg);

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
            break;
        }

        case MG_EV_OPEN:
        {
            ESP_LOGD(TAG, "WebSocket connection opened");
            break;
        }

        case MG_EV_WS_OPEN:
        {
            ESP_LOGI(TAG, "WebSocket handshake completed");
            client->state_.store(WebSocketState::CONNECTED);
            client->reconnect_attempts_ = 0;
            client->last_ping_time_ = getCurrentTimestamp();
            client->last_pong_time_ = client->last_ping_time_;

            if (client->state_callback_)
            {
                client->state_callback_(WebSocketState::CONNECTED);
            }
            break;
        }

        case MG_EV_WS_MSG:
        {
            struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);

            if (!client->message_callback_)
            {
                break;
            }

            // Create message and determine if it's binary or text
            WebSocketMessage msg;
            msg.data.assign(wm->data.ptr, wm->data.ptr + wm->data.len);

            // Determine message type based on opcode (lower 4 bits of flags)
            uint8_t opcode = wm->flags & 0x0F;
            if (opcode == 0x01)  // Text frame
            {
                msg.is_binary = false;
            }
            else if (opcode == 0x02)  // Binary frame
            {
                msg.is_binary = true;
            }
            else
            {
                // Default to text for continuation frames or unknown
                msg.is_binary = false;
            }

            ESP_LOGD(TAG, "WebSocket message received: %zu bytes, is_binary: %d",
                     msg.data.size(), msg.is_binary);

            client->message_callback_(msg);
            break;
        }

        case MG_EV_WS_CTL:
        {
            struct mg_ws_message* wm = static_cast<struct mg_ws_message*>(ev_data);
            uint8_t opcode = wm->flags & 0x0F;

            if (opcode == 0x0A)  // Pong frame
            {
                ESP_LOGD(TAG, "WebSocket pong received");
                client->last_pong_time_ = getCurrentTimestamp();
            }
            else if (opcode == 0x09)  // Ping frame
            {
                ESP_LOGD(TAG, "WebSocket ping received (mongoose auto-responds)");
            }
            else if (opcode == 0x08)  // Close frame
            {
                ESP_LOGD(TAG, "WebSocket close frame received");
            }
            break;
        }

        case MG_EV_CLOSE:
        {
            ESP_LOGI(TAG, "WebSocket connection closed");

            WebSocketState prev_state = client->state_.load();
            client->state_.store(WebSocketState::DISCONNECTED);
            client->conn_ = nullptr;

            if (client->close_callback_)
            {
                client->close_callback_(WebSocketCloseReason::NORMAL_CLOSURE, "Connection closed");
            }

            if (client->state_callback_ && prev_state != WebSocketState::DISCONNECTED)
            {
                client->state_callback_(WebSocketState::DISCONNECTED);
            }

            // Schedule reconnect if it was an unexpected disconnect
            if (prev_state == WebSocketState::CONNECTED ||
                prev_state == WebSocketState::CONNECTING)
            {
                client->scheduleReconnect();
            }
            break;
        }

        default:
            break;
    }
}

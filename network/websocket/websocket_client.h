#pragma once

#include "websocket_types.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <memory>

struct mg_mgr;
struct mg_connection;

class WebSocketClient
{
public:
    WebSocketClient();
    ~WebSocketClient();

    NetworkResult init();
    void cleanup();

    NetworkResult connect(const WebSocketConfig& config);
    void disconnect();

    NetworkResult sendText(const std::string& message);
    NetworkResult sendBinary(const std::vector<uint8_t>& data);
    NetworkResult sendJson(const std::string& json);
    NetworkResult ping(const std::string& data = "");

    WebSocketState getState() const;
    bool isConnected() const;

    void setMessageCallback(WebSocketMessageCallback callback);
    void setStateCallback(WebSocketStateCallback callback);
    void setCloseCallback(WebSocketCloseCallback callback);
    void setErrorCallback(NetworkErrorCallback callback);

public:
    static void websocketEventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data);

private:
    void serviceThread();
    void reconnectThread();
    void destroyManager();
    void scheduleReconnect();
    void processOutgoingMessages();

    struct mg_mgr* mgr_;
    struct mg_connection* conn_;

    std::atomic<WebSocketState> state_;
    std::atomic<bool> running_;
    std::atomic<bool> should_reconnect_;

    std::thread service_thread_;
    std::thread reconnect_thread_;

    mutable std::mutex config_mutex_;
    mutable std::mutex messages_mutex_;

    WebSocketConfig current_config_;
    std::queue<WebSocketMessage> outgoing_messages_;

    uint32_t reconnect_attempts_;
    uint64_t last_ping_time_;
    uint64_t last_pong_time_;

    WebSocketMessageCallback message_callback_;
    WebSocketStateCallback state_callback_;
    WebSocketCloseCallback close_callback_;
    NetworkErrorCallback error_callback_;
};
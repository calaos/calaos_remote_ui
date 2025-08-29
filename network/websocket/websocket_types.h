#pragma once

#include "network_types.h"
#include <string>
#include <map>

enum class WebSocketState
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
    CLOSING
};

enum class WebSocketCloseReason
{
    NORMAL_CLOSURE = 1000,
    GOING_AWAY = 1001,
    PROTOCOL_ERROR = 1002,
    UNSUPPORTED_DATA = 1003,
    INVALID_FRAME_PAYLOAD = 1007,
    POLICY_VIOLATION = 1008,
    MESSAGE_TOO_BIG = 1009,
    MANDATORY_EXTENSION = 1010,
    INTERNAL_ERROR = 1011,
    SERVICE_RESTART = 1012,
    TRY_AGAIN_LATER = 1013,
    BAD_GATEWAY = 1014,
    TLS_HANDSHAKE = 1015
};

struct WebSocketMessage
{
    std::string data;
    bool is_binary;

    WebSocketMessage() : is_binary(false) {}
    WebSocketMessage(const std::string& text) : data(text), is_binary(false) {}
    WebSocketMessage(const std::vector<uint8_t>& binary_data)
        : data(binary_data.begin(), binary_data.end()), is_binary(true) {}
};

struct WebSocketConfig
{
    std::string url;
    std::map<std::string, std::string> headers;
    std::vector<std::string> protocols;
    uint32_t connect_timeout_ms;
    uint32_t ping_interval_ms;
    uint32_t pong_timeout_ms;
    bool verify_ssl;
    bool auto_reconnect;
    uint32_t reconnect_delay_ms;
    uint32_t max_reconnect_attempts;

    WebSocketConfig()
        : connect_timeout_ms(30000)
        , ping_interval_ms(30000)
        , pong_timeout_ms(10000)
        , verify_ssl(true)
        , auto_reconnect(false)
        , reconnect_delay_ms(5000)
        , max_reconnect_attempts(3)
    {}
};

using WebSocketMessageCallback = std::function<void(const WebSocketMessage& message)>;
using WebSocketStateCallback = std::function<void(WebSocketState state)>;
using WebSocketCloseCallback = std::function<void(WebSocketCloseReason reason, const std::string& message)>;
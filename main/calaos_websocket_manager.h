#pragma once

#include "calaos_protocol.h"
#include "calaos_net.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

/**
 * @brief Manager for WebSocket connection to Calaos server
 *
 * Handles:
 * - Connection with HMAC authentication
 * - Message parsing and dispatching
 * - Automatic reconnection (except on auth failures)
 * - IO state commands
 */
class CalaosWebSocketManager
{
public:
    CalaosWebSocketManager();
    ~CalaosWebSocketManager();

    /**
     * @brief Connect to Calaos server using stored credentials
     * @return true if connection initiated successfully
     */
    bool connect();

    /**
     * @brief Disconnect from server
     */
    void disconnect();

    /**
     * @brief Check if currently connected
     */
    bool isConnected() const;

    /**
     * @brief Check if currently connecting
     */
    bool isConnecting() const;

    /**
     * @brief Send IO state change command to server
     * @param io_id IO identifier
     * @param state New state value
     * @return true if message sent successfully
     */
    bool setIoState(const std::string& io_id, const std::string& state);

    /**
     * @brief Request configuration from server
     * @return true if request sent successfully
     */
    bool requestConfig();

private:
    /**
     * @brief Build WebSocket URL from server URL
     * @param serverUrl Server IP or hostname
     * @return WebSocket URL (ws://host:port/path)
     */
    std::string buildWebSocketUrl(const std::string& serverUrl);

    /**
     * @brief Build authentication headers with HMAC
     * @return Map of header name -> value
     */
    std::map<std::string, std::string> buildAuthHeaders();

    /**
     * @brief WebSocket message callback
     */
    void onMessage(const WebSocketMessage& message);

    /**
     * @brief WebSocket state change callback
     */
    void onStateChanged(WebSocketState state);

    /**
     * @brief WebSocket close callback
     */
    void onClose(int code, const std::string& reason);

    /**
     * @brief WebSocket error callback
     */
    void onError(NetworkResult error, const std::string& message);

    /**
     * @brief Handle remote_ui_io_states message (batch update)
     */
    void handleIoStates(const nlohmann::json& data);

    /**
     * @brief Handle io_state message (single update)
     */
    void handleIoState(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_config_update message
     */
    void handleConfigUpdate(const nlohmann::json& data);

    /**
     * @brief Check if error indicates authentication failure
     */
    bool isAuthenticationError(int closeCode, const std::string& reason);

    WebSocketState currentState_;
    bool isConnecting_;
};

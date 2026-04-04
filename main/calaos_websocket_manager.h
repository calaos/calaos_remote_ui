#pragma once

#include "calaos_protocol.h"
#include "calaos_net.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

// Global pointer to WebSocket manager instance (set by StartupPage)
extern class CalaosWebSocketManager* g_wsManager;

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

    /**
     * @brief Send relay state to server
     * @param relay Relay number (1-indexed)
     * @param state Relay state
     * @return true if message sent successfully
     */
    bool sendRelayState(int relay, bool state);

private:
    /**
     * @brief Build WebSocket URL from server URL
     * @param serverUrl Server IP or hostname
     * @param port Server port
     * @param ssl Use wss:// if true
     * @return WebSocket URL (ws[s]://host:port/path)
     */
    std::string buildWebSocketUrl(const std::string& serverUrl, uint16_t port = 5454, bool ssl = false);

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
     * @brief Handle event message
     */
    void handleEvent(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_fw_update_available message
     */
    void handleFirmwareUpdateAvailable(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_set_brightness message
     */
    void handleSetBrightness(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_set_page message
     */
    void handleSetPage(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_notification message
     */
    void handleNotification(const nlohmann::json& data);

    /**
     * @brief Handle remote_ui_set_relay message
     */
    void handleSetRelay(const nlohmann::json& data);

    /**
     * @brief Check if error indicates authentication failure
     */
    bool isAuthenticationError(int closeCode, const std::string& reason);

    /**
     * @brief Check if error is a handshake error (potential auth failure)
     */
    bool isHandshakeError(const std::string& message);

    WebSocketState currentState_;
    bool isConnecting_;
    int consecutiveHandshakeErrors_;  // Track consecutive handshake failures
};

#include "calaos_websocket_manager.h"
#include "hmac_authenticator.h"
#include "provisioning_manager.h"
#include "app_dispatcher.h"
#include "logging.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

static const char* TAG = "ws.mgr";

// Global WebSocket manager instance
CalaosWebSocketManager* g_wsManager = nullptr;

// WebSocket close codes for authentication failures
static const int WS_CLOSE_UNAUTHORIZED = 4001;
static const int WS_CLOSE_FORBIDDEN = 4003;

CalaosWebSocketManager::CalaosWebSocketManager():
    currentState_(WebSocketState::DISCONNECTED),
    isConnecting_(false),
    consecutiveHandshakeErrors_(0)
{
}

CalaosWebSocketManager::~CalaosWebSocketManager()
{
    disconnect();
}

bool CalaosWebSocketManager::connect()
{
    if (isConnected() || isConnecting_)
    {
        ESP_LOGW(TAG, "Already connected or connecting");
        return false;
    }

    // Ensure CalaosNet is initialized before using WebSocket client
    if (!CalaosNet::instance().isInitialized())
    {
        ESP_LOGI(TAG, "Initializing CalaosNet for WebSocket connection");
        NetworkResult result = CalaosNet::instance().init();
        if (result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize CalaosNet: %d", static_cast<int>(result));
            return false;
        }
    }

    ProvisioningManager& provMgr = getProvisioningManager();
    if (!provMgr.isProvisioned())
    {
        ESP_LOGE(TAG, "Cannot connect: device not provisioned");
        return false;
    }

    std::string serverUrl = provMgr.getServerUrl();
    if (serverUrl.empty())
    {
        ESP_LOGE(TAG, "Cannot connect: empty server URL");
        return false;
    }

    // Build WebSocket URL
    std::string wsUrl = buildWebSocketUrl(serverUrl);
    ESP_LOGI(TAG, "Connecting to WebSocket: %s", wsUrl.c_str());

    // Build authentication headers
    auto headers = buildAuthHeaders();

    // Reset handshake error counter for new connection attempt
    consecutiveHandshakeErrors_ = 0;

    // Get WebSocket client and ensure auto-reconnect is enabled for fresh connections
    WebSocketClient& wsClient = CalaosNet::instance().webSocketClient();
    wsClient.setAutoReconnect(true);

    // Configure WebSocket client
    WebSocketConfig config;
    config.url = wsUrl;
    config.headers = headers;
    config.connect_timeout_ms = 30000;
    config.ping_interval_ms = 30000;
    config.pong_timeout_ms = 10000;
    config.verify_ssl = false;  // TODO: Support SSL verification
    config.auto_reconnect = true;
    config.reconnect_delay_ms = 5000;
    config.max_reconnect_attempts = 5;

    // Set callbacks
    wsClient.setMessageCallback([this](const WebSocketMessage& msg)
    {
        onMessage(msg);
    });

    wsClient.setStateCallback([this](WebSocketState state)
    {
        onStateChanged(state);
    });

    wsClient.setCloseCallback([this](WebSocketCloseReason reason, const std::string& message)
    {
        onClose(static_cast<int>(reason), message);
    });

    wsClient.setErrorCallback([this](NetworkResult error, const std::string& message)
    {
        onError(error, message);
    });

    // Set reconnect config callback to regenerate auth headers on each reconnection
    // This ensures fresh nonce/timestamp are used, avoiding "nonce reuse" errors
    wsClient.setReconnectConfigCallback([this, config]() -> WebSocketConfig
    {
        WebSocketConfig freshConfig = config;
        freshConfig.headers = buildAuthHeaders();  // Regenerate nonce/timestamp/HMAC
        ESP_LOGD(TAG, "Regenerated auth headers for reconnection");
        return freshConfig;
    });

    // Dispatch connecting event
    isConnecting_ = true;
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::WebSocketConnecting));

    // Initiate connection
    NetworkResult result = wsClient.connect(config);
    if (result != NetworkResult::OK)
    {
        ESP_LOGE(TAG, "Failed to initiate connection: %d", static_cast<int>(result));
        isConnecting_ = false;
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::WebSocketError,
                    WebSocketErrorData{"Failed to initiate connection"})
        );
        return false;
    }

    return true;
}

void CalaosWebSocketManager::disconnect()
{
    WebSocketClient& wsClient = CalaosNet::instance().webSocketClient();
    wsClient.disconnect();
    currentState_ = WebSocketState::DISCONNECTED;
    isConnecting_ = false;
}

bool CalaosWebSocketManager::isConnected() const
{
    return currentState_ == WebSocketState::CONNECTED;
}

bool CalaosWebSocketManager::isConnecting() const
{
    return isConnecting_;
}

bool CalaosWebSocketManager::setIoState(const std::string& io_id, const std::string& state)
{
    if (!isConnected())
    {
        ESP_LOGW(TAG, "Cannot send IO state: not connected");
        return false;
    }

    try
    {
        json j;
        j["msg"] = CalaosProtocol::MSG_SET_STATE;
        j["data"]["id"] = io_id;
        j["data"]["value"] = state;

        std::string message = j.dump();
        ESP_LOGD(TAG, "Sending IO state: %s", message.c_str());

        WebSocketClient& wsClient = CalaosNet::instance().webSocketClient();
        NetworkResult result = wsClient.sendJson(message);

        return result == NetworkResult::OK;
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Failed to build set_state message: %s", e.what());
        return false;
    }
}

bool CalaosWebSocketManager::requestConfig()
{
    if (!isConnected())
    {
        ESP_LOGW(TAG, "Cannot request config: not connected");
        return false;
    }

    try
    {
        json j;
        j["msg"] = CalaosProtocol::MSG_GET_CONFIG;

        std::string message = j.dump();
        ESP_LOGD(TAG, "Requesting config");

        WebSocketClient& wsClient = CalaosNet::instance().webSocketClient();
        NetworkResult result = wsClient.sendJson(message);

        return result == NetworkResult::OK;
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Failed to build get_config message: %s", e.what());
        return false;
    }
}

std::string CalaosWebSocketManager::buildWebSocketUrl(const std::string& serverUrl)
{
    std::ostringstream oss;
    oss << "ws://" << serverUrl << ":" << CalaosProtocol::WS_PORT << CalaosProtocol::WS_ENDPOINT;
    return oss.str();
}

std::map<std::string, std::string> CalaosWebSocketManager::buildAuthHeaders()
{
    ProvisioningManager& provMgr = getProvisioningManager();

    std::string authToken = provMgr.getAuthToken();
    std::string deviceSecret = provMgr.getDeviceSecret();

    // Generate timestamp and nonce
    uint64_t timestamp = HMACAuthenticator::getTimestamp();
    std::string nonce = HMACAuthenticator::generateNonce();

    // Compute HMAC: HMAC-SHA256(device_secret, auth_token + ":" + timestamp + ":" + nonce)
    std::string dataToSign = authToken + ":" + std::to_string(timestamp) + ":" + nonce;
    std::string hmac = HMACAuthenticator::computeHmacSha256(deviceSecret, dataToSign);

    ESP_LOGD(TAG, "Auth - Token: %s", authToken.c_str());
    ESP_LOGD(TAG, "Auth - Timestamp: %lu", (unsigned long)timestamp);
    ESP_LOGD(TAG, "Auth - Nonce: %s", nonce.c_str());
    ESP_LOGD(TAG, "Auth - HMAC: %s", hmac.c_str());

    std::map<std::string, std::string> headers;
    headers[CalaosProtocol::AUTH_HEADER_TOKEN] = "Bearer " + authToken;
    headers[CalaosProtocol::AUTH_HEADER_TIMESTAMP] = std::to_string(timestamp);
    headers[CalaosProtocol::AUTH_HEADER_NONCE] = nonce;
    headers[CalaosProtocol::AUTH_HEADER_HMAC] = hmac;

    return headers;
}

void CalaosWebSocketManager::onMessage(const WebSocketMessage& message)
{
    ESP_LOGD(TAG, "Received message: %s", message.data.c_str());

    try
    {
        json j = json::parse(message.data);

        if (!j.contains("msg"))
        {
            ESP_LOGW(TAG, "Message missing 'msg' field");
            return;
        }

        std::string msgType = j["msg"];

        if (msgType == CalaosProtocol::MSG_IO_STATES)
            handleIoStates(j.value("data", json::object()));
        else if (msgType == CalaosProtocol::MSG_IO_STATE)
            handleIoState(j.value("data", json::object()));
        else if (msgType == CalaosProtocol::MSG_CONFIG_UPDATE)
            handleConfigUpdate(j.value("data", json::object()));
        else if (msgType == CalaosProtocol::MSG_EVENT)
            handleEvent(j.value("data", json::object()));
        else
            ESP_LOGW(TAG, "Unknown message type: %s", msgType.c_str());
    }
    catch (const json::parse_error& e)
    {
        ESP_LOGE(TAG, "JSON parse error: %s", e.what());
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error handling message: %s", e.what());
    }
}

void CalaosWebSocketManager::onStateChanged(WebSocketState state)
{
    ESP_LOGI(TAG, "State changed: %d", static_cast<int>(state));
    currentState_ = state;

    if (state == WebSocketState::CONNECTED)
    {
        isConnecting_ = false;
        consecutiveHandshakeErrors_ = 0;  // Reset on successful connection
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::WebSocketConnected));
    }
    else if (state == WebSocketState::CONNECTING)
    {
        isConnecting_ = true;
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::WebSocketConnecting));
    }
    else if (state == WebSocketState::DISCONNECTED)
    {
        isConnecting_ = false;
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::WebSocketDisconnected,
                    WebSocketDisconnectedData{"Disconnected", 0})
        );
    }
}

void CalaosWebSocketManager::onClose(int code, const std::string& reason)
{
    ESP_LOGI(TAG, "Connection closed: code=%d, reason=%s", code, reason.c_str());

    isConnecting_ = false;
    currentState_ = WebSocketState::DISCONNECTED;

    // Check if this is an authentication failure
    if (isAuthenticationError(code, reason))
    {
        ESP_LOGE(TAG, "Authentication failed - returning to provisioning");

        // Disable auto-reconnect before dispatching to prevent reconnect attempts with bad credentials
        CalaosNet::instance().webSocketClient().setAutoReconnect(false);

        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::WebSocketAuthFailed,
                    WebSocketAuthFailedData{reason})
        );
    }
    else
    {
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::WebSocketDisconnected,
                    WebSocketDisconnectedData{reason, code})
        );
    }
}

void CalaosWebSocketManager::onError(NetworkResult error, const std::string& message)
{
    ESP_LOGE(TAG, "WebSocket error: %d - %s", static_cast<int>(error), message.c_str());

    // Track consecutive handshake errors - they often indicate auth failures
    // (e.g., server rejecting connection due to invalid HMAC)
    if (isHandshakeError(message))
    {
        consecutiveHandshakeErrors_++;
        ESP_LOGW(TAG, "Handshake error count: %d", consecutiveHandshakeErrors_);

        // After 3 consecutive handshake failures, assume auth failure
        if (consecutiveHandshakeErrors_ >= 3)
        {
            ESP_LOGE(TAG, "Too many handshake failures - assuming authentication failure");

            // Disable auto-reconnect before dispatching to prevent further attempts
            CalaosNet::instance().webSocketClient().setAutoReconnect(false);

            AppDispatcher::getInstance().dispatch(
                AppEvent(AppEventType::WebSocketAuthFailed,
                        WebSocketAuthFailedData{"Multiple handshake failures - credentials may be invalid"})
            );
            return;
        }
    }

    AppDispatcher::getInstance().dispatch(
        AppEvent(AppEventType::WebSocketError,
                WebSocketErrorData{message})
    );
}

void CalaosWebSocketManager::handleIoStates(const json& data)
{
    ESP_LOGI(TAG, "Handling IO states batch");
    ESP_LOGD(TAG, "IO states data type: %s", data.type_name());

    try
    {
        std::map<std::string, CalaosProtocol::IoState> ioStates;

        // Handle both array format [{io_id: "x", ...}, ...] and object format {id: {...}, ...}
        if (data.is_array())
        {
            // Array format: each element is an IO state object
            for (const auto& ioData : data)
            {
                // Try both "io_id" and "id" keys for compatibility
                std::string ioId = ioData.value("io_id", "");
                if (ioId.empty())
                    ioId = ioData.value("id", "");

                if (ioId.empty())
                {
                    ESP_LOGW(TAG, "IO state missing 'io_id'/'id' field, skipping");
                    continue;
                }

                CalaosProtocol::IoState ioState;
                ioState.id = ioId;
                ioState.type = ioData.value("type", "");

                // Handle state which can be string or bool
                if (ioData.contains("state"))
                {
                    if (ioData["state"].is_boolean())
                        ioState.state = ioData["state"].get<bool>() ? "true" : "false";
                    else
                        ioState.state = ioData.value("state", "");
                }

                ioState.gui_type = ioData.value("gui_type", "");
                ioState.name = ioData.value("name", "");
                ioState.visible = ioData.value("visible", true);
                ioState.enabled = ioData.value("enabled", true);

                ioStates[ioId] = ioState;
            }
        }
        else if (data.is_object())
        {
            for (auto it = data.begin(); it != data.end(); ++it)
            {
                const std::string& ioId = it.key();
                const json& ioData = it.value();

                CalaosProtocol::IoState ioState;
                ioState.id = ioId;

                if (ioData.is_string())
                {
                    ioState.state = ioData.get<std::string>();
                }
                else if (ioData.is_boolean())
                {
                    ioState.state = ioData.get<bool>() ? "true" : "false";
                }
                else if (ioData.is_object())
                {
                    ioState.id = ioData.value("id", ioId);
                    ioState.type = ioData.value("type", "");

                    if (ioData.contains("state"))
                    {
                        if (ioData["state"].is_boolean())
                            ioState.state = ioData["state"].get<bool>() ? "true" : "false";
                        else
                            ioState.state = ioData.value("state", "");
                    }

                    ioState.gui_type = ioData.value("gui_type", "");
                    ioState.name = ioData.value("name", "");
                    ioState.visible = ioData.value("visible", true);
                    ioState.enabled = ioData.value("enabled", true);
                }

                ioStates[ioId] = ioState;
            }
        }
        else
        {
            ESP_LOGW(TAG, "IO states data is neither array nor object, ignoring");
            return;
        }

        ESP_LOGI(TAG, "Parsed %zu IO states", ioStates.size());

        // Dispatch event
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::IoStatesReceived,
                    IoStatesReceivedData{ioStates})
        );
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error parsing IO states: %s", e.what());
    }
}

void CalaosWebSocketManager::handleIoState(const json& data)
{
    try
    {
        std::string ioId = data.value("io_id", "");

        // Handle state which can be string or bool
        std::string state;
        if (data.contains("state"))
        {
            if (data["state"].is_boolean())
                state = data["state"].get<bool>() ? "true" : "false";
            else
                state = data.value("state", "");
        }

        if (ioId.empty())
        {
            ESP_LOGW(TAG, "IO state update missing io_id");
            return;
        }

        ESP_LOGI(TAG, "IO state update: %s = %s", ioId.c_str(), state.c_str());

        // Create IoState with minimal info (will be merged with existing)
        CalaosProtocol::IoState ioState;
        ioState.id = ioId;
        ioState.state = state;

        // Dispatch event
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::IoStateReceived,
                    IoStateReceivedData{ioState})
        );
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error parsing IO state: %s", e.what());
    }
}

void CalaosWebSocketManager::handleConfigUpdate(const json& data)
{
    try
    {
        CalaosProtocol::RemoteUIConfig config;
        config.name = data.value("name", "");

        // Handle room (can be string or object)
        if (data.contains("room"))
        {
            if (data["room"].is_string())
                config.room = data["room"].get<std::string>();
            else if (data["room"].is_object() && data["room"].contains("name"))
                config.room = data["room"]["name"].get<std::string>();
        }

        config.theme = data.value("theme", "dark");

        // Handle brightness typo in server (brigtness instead of brightness)
        if (data.contains("brigtness"))
            config.brightness = data["brigtness"].get<int>();
        else
            config.brightness = data.value("brightness", 80);

        config.timeout = data.value("timeout", 30);

        // Build pages_json with grid info from root level
        json pagesConfig;
        pagesConfig["grid_width"] = data.value("grid_width", 3);
        pagesConfig["grid_height"] = data.value("grid_height", 3);

        if (data.contains("pages"))
            pagesConfig["pages"] = data["pages"];
        else
            pagesConfig["pages"] = json::array();

        config.pages_json = pagesConfig.dump();

        ESP_LOGI(TAG, "Config update: name=%s, grid=%dx%d, pages=%zu",
                config.name.c_str(),
                pagesConfig["grid_width"].get<int>(),
                pagesConfig["grid_height"].get<int>(),
                pagesConfig["pages"].size());

        // Handle io_items if present (store IO states)
        if (data.contains("io_items") && data["io_items"].is_array())
        {
            for (const auto& ioItem : data["io_items"])
            {
                CalaosProtocol::IoState ioState;
                ioState.id = ioItem.value("id", "");
                ioState.type = ioItem.value("type", "");
                ioState.gui_type = ioItem.value("gui_type", "");
                ioState.name = ioItem.value("name", "");
                ioState.visible = ioItem.value("visible", "true") == "true";
                ioState.enabled = ioItem.value("rw", "true") == "true";
                ioState.state = "false";  // Default state

                // Dispatch individual IO state
                AppDispatcher::getInstance().dispatch(
                    AppEvent(AppEventType::IoStateReceived,
                            IoStateReceivedData{ioState})
                );
            }
        }

        // Dispatch config event
        AppDispatcher::getInstance().dispatch(
            AppEvent(AppEventType::ConfigUpdateReceived,
                    ConfigUpdateReceivedData{config})
        );
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error parsing config update: %s", e.what());
    }
}

void CalaosWebSocketManager::handleEvent(const json& data)
{
    try
    {
        std::string typeStr = data.value("type_str", "");

        if (typeStr == "io_changed")
        {
            if (!data.contains("data") || !data["data"].is_object())
            {
                ESP_LOGW(TAG, "Event io_changed missing data object");
                return;
            }

            const json& eventData = data["data"];
            std::string ioId = eventData.value("id", "");
            std::string state = eventData.value("state", "");

            if (ioId.empty())
            {
                ESP_LOGW(TAG, "Event io_changed missing id");
                return;
            }

            ESP_LOGI(TAG, "Event io_changed: %s = %s", ioId.c_str(), state.c_str());

            CalaosProtocol::IoState ioState;
            ioState.id = ioId;
            ioState.state = state;

            AppDispatcher::getInstance().dispatch(
                AppEvent(AppEventType::IoStateReceived,
                        IoStateReceivedData{ioState})
            );
        }
        else
        {
            ESP_LOGD(TAG, "Ignoring event type: %s", typeStr.c_str());
        }
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error parsing event: %s", e.what());
    }
}

bool CalaosWebSocketManager::isAuthenticationError(int closeCode, const std::string& reason)
{
    // Check for specific close codes indicating auth failure
    if (closeCode == WS_CLOSE_UNAUTHORIZED || closeCode == WS_CLOSE_FORBIDDEN)
        return true;

    // Check for authentication-related keywords in reason
    std::string lowerReason = reason;
    std::transform(lowerReason.begin(), lowerReason.end(), lowerReason.begin(), ::tolower);

    return lowerReason.find("auth") != std::string::npos ||
           lowerReason.find("unauthorized") != std::string::npos ||
           lowerReason.find("forbidden") != std::string::npos ||
           lowerReason.find("invalid") != std::string::npos ||
           lowerReason.find("hmac") != std::string::npos ||
           lowerReason.find("signature") != std::string::npos ||
           lowerReason.find("token") != std::string::npos;
}

bool CalaosWebSocketManager::isHandshakeError(const std::string& message)
{
    std::string lowerMessage = message;
    std::transform(lowerMessage.begin(), lowerMessage.end(), lowerMessage.begin(), ::tolower);
    return lowerMessage.find("handshake") != std::string::npos;
}

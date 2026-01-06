#pragma once

#include <string>
#include <variant>
#include <memory>
#include <map>
#include "calaos_protocol.h"

enum class AppEventType
{
    NetworkStatusChanged,
    NetworkIpAssigned,
    NetworkDisconnected,
    NetworkTimeout,

    // NTP time synchronization events
    NtpSyncStarted,
    NtpTimeSynced,
    NtpSyncFailed,

    CalaosDiscoveryStarted,
    CalaosServerFound,
    CalaosDiscoveryTimeout,
    CalaosDiscoveryStopped,

    ProvisioningCodeGenerated,
    ProvisioningCompleted,
    ProvisioningFailed,
    ProvisioningVerifyStarted,
    ProvisioningVerifyFailed,

    WebSocketConnecting,
    WebSocketConnected,
    WebSocketDisconnected,
    WebSocketAuthFailed,
    WebSocketError,

    IoStateReceived,
    IoStatesReceived,
    ConfigUpdateReceived,
};

enum class NetworkConnectionType
{
    None,
    WiFi,
    Ethernet
};

struct NetworkStatusChangedData
{
    bool isConnected;
    NetworkConnectionType connectionType;
};

struct NetworkIpAssignedData
{
    std::string ipAddress;
    std::string gateway;
    std::string netmask;
    NetworkConnectionType connectionType;
    // WiFi specific info
    std::string ssid;
    int rssi;
};

struct CalaosServerFoundData
{
    std::string serverIp;
};

struct ProvisioningCodeGeneratedData
{
    std::string provisioningCode;
    std::string macAddress;
};

struct ProvisioningCompletedData
{
    std::string deviceId;
    std::string serverUrl;
};

struct ProvisioningFailedData
{
    std::string errorMessage;
};

struct ProvisioningVerifyFailedData
{
    std::string errorMessage;
    bool isNetworkError;  // true if network error, false if invalid credentials
};

struct WebSocketDisconnectedData
{
    std::string reason;
    int code;
};

// WebSocket authentication error types based on server response
enum class WebSocketAuthErrorType
{
    Unknown,           // Unknown error
    InvalidToken,      // Token unknown/deleted on server - requires re-provisioning
    InvalidHmac,       // HMAC mismatch (wrong secret) - requires re-provisioning
    InvalidTimestamp,  // Clock out of sync (>30s drift) - retry after NTP sync
    InvalidNonce,      // Nonce reused or wrong format - retry with new nonce
    MissingHeaders,    // Required auth headers not provided - code issue, retry
    RateLimited,       // Too many attempts - wait and retry
    NetworkError,      // Network/connection error - retry with backoff
    HandshakeFailure   // Multiple handshake failures - may require re-provisioning
};

struct WebSocketAuthFailedData
{
    std::string message;
    WebSocketAuthErrorType errorType = WebSocketAuthErrorType::Unknown;
    int httpCode = 0;  // HTTP error code if available (401, 403, 429, etc.)
    std::string errorString;  // Raw error string from server (e.g., "invalid_token")

    // Helper to check if this error requires re-provisioning
    bool requiresReProvisioning() const
    {
        return errorType == WebSocketAuthErrorType::InvalidToken ||
               errorType == WebSocketAuthErrorType::InvalidHmac ||
               errorType == WebSocketAuthErrorType::HandshakeFailure;
    }

    // Helper to check if this error is retryable
    bool isRetryable() const
    {
        return errorType == WebSocketAuthErrorType::InvalidTimestamp ||
               errorType == WebSocketAuthErrorType::InvalidNonce ||
               errorType == WebSocketAuthErrorType::MissingHeaders ||
               errorType == WebSocketAuthErrorType::RateLimited ||
               errorType == WebSocketAuthErrorType::NetworkError;
    }

    // Get suggested retry delay in milliseconds (for rate limiting)
    int getRetryDelayMs() const
    {
        if (errorType == WebSocketAuthErrorType::RateLimited)
            return 60000;  // 60 seconds for rate limiting
        return 5000;  // 5 seconds for other retryable errors
    }
};

struct WebSocketErrorData
{
    std::string errorMessage;
};

struct IoStateReceivedData
{
    CalaosProtocol::IoState ioState;
};

struct IoStatesReceivedData
{
    std::map<std::string, CalaosProtocol::IoState> ioStates;
};

struct ConfigUpdateReceivedData
{
    CalaosProtocol::RemoteUIConfig config;
};

using AppEventData = std::variant<
    std::monostate,  // For events without data
    NetworkStatusChangedData,
    NetworkIpAssignedData,
    CalaosServerFoundData,
    ProvisioningCodeGeneratedData,
    ProvisioningCompletedData,
    ProvisioningFailedData,
    ProvisioningVerifyFailedData,
    WebSocketDisconnectedData,
    WebSocketAuthFailedData,
    WebSocketErrorData,
    IoStateReceivedData,
    IoStatesReceivedData,
    ConfigUpdateReceivedData
>;

class AppEvent
{
public:
    AppEvent(AppEventType type) : type_(type), data_(std::monostate{}) {}

    template<typename T>
    AppEvent(AppEventType type, const T& data) : type_(type), data_(data) {}

    AppEventType getType() const { return type_; }

    template<typename T>
    const T* getData() const
    {
        return std::get_if<T>(&data_);
    }

    bool hasData() const
    {
        return !std::holds_alternative<std::monostate>(data_);
    }

private:
    AppEventType type_;
    AppEventData data_;
};
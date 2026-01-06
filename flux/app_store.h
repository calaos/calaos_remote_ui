#pragma once

#include "app_event.h"
#include "thread_safety.h"
#include "calaos_protocol.h"
#include <string>
#include <functional>
#include <vector>
#include <map>

struct NetworkState
{
    bool isConnected = false;
    bool isReady = false;
    bool hasTimeout = false;
    NetworkConnectionType connectionType = NetworkConnectionType::None;
    std::string ipAddress;
    std::string gateway;
    std::string netmask;

    // WiFi specific info
    std::string ssid;
    int rssi = 0;
};

struct NtpState
{
    bool isSyncing = false;    // NTP sync in progress
    bool isSynced = false;     // Time has been synchronized
    bool hasFailed = false;    // Last sync attempt failed (will retry)
};

struct CalaosServerState
{
    bool isDiscovering = false;
    bool hasTimeout = false;
    std::vector<std::string> discoveredServers;
    std::string selectedServer;

    bool hasServers() const
    {
        return !discoveredServers.empty();
    }

    void addServer(const std::string& serverIp)
    {
        // Avoid duplicates
        for (const auto& server : discoveredServers)
        {
            if (server == serverIp)
                return;
        }
        discoveredServers.push_back(serverIp);

        // Auto-select first server found
        if (selectedServer.empty())
            selectedServer = serverIp;
    }
};

enum class ProvisioningStatus
{
    NotProvisioned,    // Device needs provisioning
    Verifying,         // Verifying existing credentials
    ShowingCode,       // Displaying provisioning code
    Provisioned        // Device is provisioned and ready
};

struct ProvisioningState
{
    ProvisioningStatus status = ProvisioningStatus::NotProvisioned;
    std::string provisioningCode;
    std::string macAddress;
    std::string deviceId;
    std::string serverUrl;
    bool hasFailed = false;

    bool isProvisioned() const
    {
        return status == ProvisioningStatus::Provisioned;
    }

    bool isVerifying() const
    {
        return status == ProvisioningStatus::Verifying;
    }

    bool needsCodeDisplay() const
    {
        return status == ProvisioningStatus::ShowingCode;
    }
};

struct CalaosWebSocketState
{
    bool isConnected = false;
    bool isConnecting = false;
    bool hasError = false;
    bool authFailed = false;
    std::string errorMessage;

    // Detailed auth error info (from WebSocketAuthFailedData)
    WebSocketAuthErrorType authErrorType = WebSocketAuthErrorType::Unknown;
    int authHttpCode = 0;
    std::string authErrorString;

    // Helper to check if auth error requires re-provisioning
    bool requiresReProvisioning() const
    {
        if (!authFailed)
            return false;
        return authErrorType == WebSocketAuthErrorType::InvalidToken ||
               authErrorType == WebSocketAuthErrorType::InvalidHmac ||
               authErrorType == WebSocketAuthErrorType::HandshakeFailure;
    }

    // Helper to check if auth error is retryable
    bool isRetryableError() const
    {
        if (!authFailed)
            return false;
        return authErrorType == WebSocketAuthErrorType::InvalidTimestamp ||
               authErrorType == WebSocketAuthErrorType::InvalidNonce ||
               authErrorType == WebSocketAuthErrorType::MissingHeaders ||
               authErrorType == WebSocketAuthErrorType::RateLimited ||
               authErrorType == WebSocketAuthErrorType::NetworkError;
    }

    // Get suggested retry delay in milliseconds
    int getRetryDelayMs() const
    {
        if (authErrorType == WebSocketAuthErrorType::RateLimited)
            return 60000;  // 60 seconds for rate limiting
        return 5000;  // 5 seconds for other retryable errors
    }
};

struct AppState
{
    NetworkState network;
    NtpState ntp;
    CalaosServerState calaosServer;
    ProvisioningState provisioning;
    CalaosWebSocketState websocket;
    std::map<std::string, CalaosProtocol::IoState> ioStates;
    CalaosProtocol::RemoteUIConfig config;

    bool operator==(const AppState& other) const
    {
        return network.isConnected == other.network.isConnected &&
               network.isReady == other.network.isReady &&
               network.hasTimeout == other.network.hasTimeout &&
               network.connectionType == other.network.connectionType &&
               network.ipAddress == other.network.ipAddress &&
               network.ssid == other.network.ssid &&
               network.rssi == other.network.rssi &&
               ntp.isSyncing == other.ntp.isSyncing &&
               ntp.isSynced == other.ntp.isSynced &&
               ntp.hasFailed == other.ntp.hasFailed &&
               calaosServer.isDiscovering == other.calaosServer.isDiscovering &&
               calaosServer.hasTimeout == other.calaosServer.hasTimeout &&
               calaosServer.discoveredServers == other.calaosServer.discoveredServers &&
               calaosServer.selectedServer == other.calaosServer.selectedServer &&
               provisioning.status == other.provisioning.status &&
               provisioning.provisioningCode == other.provisioning.provisioningCode &&
               provisioning.deviceId == other.provisioning.deviceId &&
               provisioning.serverUrl == other.provisioning.serverUrl &&
               provisioning.hasFailed == other.provisioning.hasFailed &&
               websocket.isConnected == other.websocket.isConnected &&
               websocket.isConnecting == other.websocket.isConnecting &&
               websocket.hasError == other.websocket.hasError &&
               websocket.authFailed == other.websocket.authFailed;
               // Note: ioStates and config not compared for performance
    }

    bool operator!=(const AppState& other) const
    {
        return !(*this == other);
    }
};

using SubscriptionId = uint32_t;
using StateChangeCallback = std::function<void(const AppState& state)>;

class AppStore
{
public:
    static AppStore& getInstance();

    // Get current state
    const AppState& getState() const;

    // Subscribe to state changes - returns subscription ID for unsubscribing
    SubscriptionId subscribe(StateChangeCallback callback);

    // Unsubscribe from state changes
    void unsubscribe(SubscriptionId id);

    // Handle events and update state
    void handleEvent(const AppEvent& event);

    // Clear all subscribers
    void clearSubscribers();

    // Shutdown the store - stops notifications and clears subscribers
    void shutdown();

    // Check if store is shutting down
    bool isShuttingDown() const;

private:
    AppStore();

    void notifyStateChange();

    AppState state_;
    std::map<SubscriptionId, StateChangeCallback> subscribers_;
    SubscriptionId nextSubscriptionId_ = 1;
    mutable flux::Mutex mutex_;
    bool shuttingDown_ = false;
};
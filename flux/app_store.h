#pragma once

#include "app_event.h"
#include "thread_safety.h"
#include <string>
#include <functional>
#include <vector>

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
    
    bool needsCodeDisplay() const
    {
        return status == ProvisioningStatus::ShowingCode;
    }
};

struct AppState
{
    NetworkState network;
    CalaosServerState calaosServer;
    ProvisioningState provisioning;

    bool operator==(const AppState& other) const
    {
        return network.isConnected == other.network.isConnected &&
               network.isReady == other.network.isReady &&
               network.hasTimeout == other.network.hasTimeout &&
               network.connectionType == other.network.connectionType &&
               network.ipAddress == other.network.ipAddress &&
               network.ssid == other.network.ssid &&
               network.rssi == other.network.rssi &&
               calaosServer.isDiscovering == other.calaosServer.isDiscovering &&
               calaosServer.hasTimeout == other.calaosServer.hasTimeout &&
               calaosServer.discoveredServers == other.calaosServer.discoveredServers &&
               calaosServer.selectedServer == other.calaosServer.selectedServer &&
               provisioning.status == other.provisioning.status &&
               provisioning.provisioningCode == other.provisioning.provisioningCode &&
               provisioning.deviceId == other.provisioning.deviceId &&
               provisioning.serverUrl == other.provisioning.serverUrl &&
               provisioning.hasFailed == other.provisioning.hasFailed;
    }

    bool operator!=(const AppState& other) const
    {
        return !(*this == other);
    }
};

using StateChangeCallback = std::function<void(const AppState& state)>;

class AppStore
{
public:
    static AppStore& getInstance();

    // Get current state
    const AppState& getState() const;

    // Subscribe to state changes
    void subscribe(StateChangeCallback callback);

    // Handle events and update state
    void handleEvent(const AppEvent& event);

    // Clear all subscribers
    void clearSubscribers();

private:
    AppStore();

    void notifyStateChange();

    AppState state_;
    std::vector<StateChangeCallback> subscribers_;
    mutable flux::Mutex mutex_;
};
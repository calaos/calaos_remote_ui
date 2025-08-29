#pragma once

#include <string>
#include <variant>
#include <memory>

enum class AppEventType
{
    NetworkStatusChanged,
    NetworkIpAssigned,
    NetworkDisconnected,
    NetworkTimeout,
    CalaosDiscoveryStarted,
    CalaosServerFound,
    CalaosDiscoveryTimeout,
    CalaosDiscoveryStopped,
    ProvisioningCodeGenerated,
    ProvisioningCompleted,
    ProvisioningFailed,
    // Add more event types as needed
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

using AppEventData = std::variant<
    std::monostate,  // For events without data
    NetworkStatusChangedData,
    NetworkIpAssignedData,
    CalaosServerFoundData,
    ProvisioningCodeGeneratedData,
    ProvisioningCompletedData
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
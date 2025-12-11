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

    CalaosDiscoveryStarted,
    CalaosServerFound,
    CalaosDiscoveryTimeout,
    CalaosDiscoveryStopped,

    ProvisioningCodeGenerated,
    ProvisioningCompleted,
    ProvisioningFailed,

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

struct WebSocketDisconnectedData
{
    std::string reason;
    int code;
};

struct WebSocketAuthFailedData
{
    std::string message;
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
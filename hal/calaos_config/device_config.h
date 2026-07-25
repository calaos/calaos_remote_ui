#pragma once

#include "calaos_config/calaos_config.h"
#include <string>

// Singleton wrapper around CalaosConfig.
// Provides a global accessor so any part of the application
// can query the device provisioning config.

class DeviceConfig
{
public:
    static DeviceConfig& getInstance()
    {
        static DeviceConfig instance;
        return instance;
    }

    // Load config via a streaming reader (calls calaosConfigParse).
    // Returns true on success, false on error (config stays at defaults).
    bool loadFromReader(CfgReader &reader)
    {
        parseError_ = calaosConfigParse(reader, config_);
        loaded_ = (parseError_ == CfgError::Ok);
        return loaded_;
    }

    // Whether a valid config was loaded
    bool isLoaded() const { return loaded_; }

    // Full config access (e.g. to pre-fill the settings UI)
    const CalaosConfig& getConfig() const { return config_; }

    // Replace the in-RAM config (used after a successful save to the
    // config partition so the running app never sees stale values).
    void setConfig(const CalaosConfig &cfg)
    {
        config_ = cfg;
        config_.hasServerHost = !config_.serverHost.empty();
        loaded_ = true;
        parseError_ = CfgError::Ok;
    }

    // Last parse error
    CfgError getParseError() const { return parseError_; }

    // Accessors
    bool isWifi() const { return config_.networkInterface == "wifi"; }
    bool isEthernet() const { return config_.networkInterface == "ethernet"; }
    bool isStaticIp() const { return config_.ipMode == "static"; }
    bool isDhcp() const { return config_.ipMode == "dhcp"; }

    const std::string& getNetworkInterface() const { return config_.networkInterface; }
    const std::string& getIpMode() const { return config_.ipMode; }
    const std::string& getStaticIp() const { return config_.staticIp; }
    const std::string& getStaticMask() const { return config_.staticMask; }
    const std::string& getStaticGateway() const { return config_.staticGateway; }
    const std::string& getStaticDns() const { return config_.staticDns; }
    const std::string& getWifiSsid() const { return config_.wifiSsid; }
    const std::string& getWifiPassword() const { return config_.wifiPassword; }
    const std::string& getServerHost() const { return config_.serverHost; }
    uint16_t getServerPort() const { return config_.serverPort; }
    bool getServerSsl() const { return config_.serverSsl; }
    bool hasServerHost() const { return config_.hasServerHost; }

private:
    DeviceConfig() = default;
    DeviceConfig(const DeviceConfig&) = delete;
    DeviceConfig& operator=(const DeviceConfig&) = delete;

    CalaosConfig config_;
    bool loaded_ = false;
    CfgError parseError_ = CfgError::Ok;
};

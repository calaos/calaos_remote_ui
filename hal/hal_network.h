#pragma once

#include "hal_types.h"
#include <vector>

class HalNetwork
{
public:
    virtual ~HalNetwork() = default;
    
    virtual HalResult init() = 0;
    virtual HalResult deinit() = 0;
    virtual HalResult scanWifi(std::vector<WifiConfig>& networks) = 0;
    virtual HalResult connectWifi(const WifiConfig& config) = 0;
    virtual HalResult disconnectWifi() = 0;
    virtual WifiStatus getWifiStatus() const = 0;
    virtual HalResult registerWifiCallback(WifiEventCallback callback) = 0;
    virtual std::string getLocalIp() const = 0;
    virtual std::string getMacAddress() const = 0;
};
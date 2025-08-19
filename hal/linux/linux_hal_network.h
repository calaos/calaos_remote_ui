#pragma once

#include "../hal_network.h"
#include <thread>
#include <atomic>

class LinuxHalNetwork : public HalNetwork {
public:
    HalResult init() override;
    HalResult deinit() override;
    HalResult scanWifi(std::vector<WifiConfig>& networks) override;
    HalResult connectWifi(const WifiConfig& config) override;
    HalResult disconnectWifi() override;
    WifiStatus getWifiStatus() const override;
    HalResult registerWifiCallback(WifiEventCallback callback) override;
    std::string getLocalIP() const override;
    std::string getMacAddress() const override;

private:
    void statusMonitorThread();
    WifiStatus checkWifiStatus();
    
    WifiStatus wifi_status_ = WifiStatus::DISCONNECTED;
    WifiEventCallback wifi_callback_;
    std::thread status_thread_;
    std::atomic<bool> thread_running_;
};
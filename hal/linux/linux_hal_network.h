#pragma once

#include "../hal_network.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

class LinuxHalNetwork : public HalNetwork {
public:
    ~LinuxHalNetwork();
    HalResult init() override;
    HalResult deinit() override;
    HalResult scanWifi(std::vector<WifiConfig>& networks) override;
    HalResult connectWifi(const WifiConfig& config) override;
    HalResult disconnectWifi() override;
    WifiStatus getWifiStatus() const override;
    HalResult registerWifiCallback(WifiEventCallback callback) override;
    std::string getLocalIp() const override;
    std::string getMacAddress() const override;

private:
    void statusMonitorThread();
    WifiStatus checkWifiStatus();

    void startNetworkTimeout();
    void stopNetworkTimeout();
    void networkTimeoutTask();

    WifiStatus wifi_status_ = WifiStatus::DISCONNECTED;
    WifiEventCallback wifi_callback_;
    std::thread status_thread_;
    std::atomic<bool> thread_running_{false};
    std::thread timeout_thread_;
    std::atomic<bool> timeout_active_{false};
    std::atomic<bool> network_connected_{false};
    std::mutex timeout_mutex_;
    std::condition_variable timeout_cv_;
    std::mutex status_mutex_;
    std::condition_variable status_cv_;
};
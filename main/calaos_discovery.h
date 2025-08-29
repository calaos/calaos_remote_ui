#pragma once

#include "calaos_net.h"
#include "flux.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#define BCAST_UDP_PORT 4545

class CalaosDiscovery
{
public:
    CalaosDiscovery();
    ~CalaosDiscovery();

    void startDiscovery();
    void stopDiscovery();

    bool isDiscovering() const;

private:
    void discoveryThread();
    void sendDiscoveryBroadcast();
    void onUdpDataReceived(NetworkResult result, const NetworkBuffer& data);
    void onDiscoveryTimeout();

    std::atomic<bool> running_;
    std::atomic<bool> discovering_;
    std::thread discovery_thread_;
    mutable std::mutex discovery_mutex_;

    std::chrono::steady_clock::time_point last_broadcast_time_;
    std::chrono::steady_clock::time_point discovery_start_time_;

    static constexpr uint32_t BROADCAST_INTERVAL_MS = 2000;  // 2 seconds
    static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 30000;  // 30 seconds

    bool udp_listening_;
};
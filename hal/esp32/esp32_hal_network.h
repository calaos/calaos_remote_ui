#pragma once

#include "../hal_network.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_eth.h"

class Esp32HalNetwork : public HalNetwork
{
public:
    HalResult init() override;
    HalResult deinit() override;
    HalResult scanWifi(std::vector<WifiConfig>& networks) override;
    HalResult connectWifi(const WifiConfig& config) override;
    HalResult disconnectWifi() override;
    WifiStatus getWifiStatus() const override;
    HalResult registerWifiCallback(WifiEventCallback callback) override;
    std::string getLocalIp() const override;
    std::string getMacAddress() const override;
    void retryConnection() override;

    // Public method for event handlers to signal network connection
    void onNetworkConnected();

private:
    HalResult initEthernet();
    HalResult initWifi();
    void setupTimeout();

    // Apply static IP configuration on a netif (called from event handlers after link-up)
    static void applyStaticIpConfig(esp_netif_t *netif, const char *ifkey);

    static void wifiEventHandler(void* arg, esp_event_base_t eventBase,
                                int32_t eventId, void* eventData);
    static void ethEventHandler(void* arg, esp_event_base_t eventBase,
                                int32_t eventId, void* eventData);

    void startNetworkTimeout();
    void stopNetworkTimeout();
    static void networkTimeoutCallback(TimerHandle_t timer);
    static void networkTimeoutTask(void* parameter);

    WifiStatus wifiStatus = WifiStatus::DISCONNECTED;
    WifiEventCallback wifiCallback;
    esp_event_handler_instance_t wifiHandlerInstance = nullptr;
    esp_event_handler_instance_t ipHandlerInstance = nullptr;
    TimerHandle_t networkTimeoutTimer = nullptr;
    bool networkConnected = false;

    // Track which subsystems are initialized
    bool ethInitialized_ = false;
    bool wifiInitialized_ = false;

    // Ethernet support
    esp_eth_handle_t ethHandle = nullptr;
    bool ethernetConnected = false;
    bool wifiConnected = false;

    // Static members for timeout handling
    static QueueHandle_t timeoutQueue;
    static TaskHandle_t timeoutTaskHandle;
    static SemaphoreHandle_t retrySemaphore_;
    static Esp32HalNetwork* retryInstance_;
};
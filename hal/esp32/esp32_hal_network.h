#pragma once

#include "../hal_network.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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
    
    // Public method for event handlers to signal network connection
    void onNetworkConnected();

private:
    static void wifiEventHandler(void* arg, esp_event_base_t eventBase, 
                                int32_t eventId, void* eventData);
    
    void startNetworkTimeout();
    void stopNetworkTimeout();
    static void networkTimeoutCallback(TimerHandle_t timer);
    static void networkTimeoutTask(void* parameter);
    
    WifiStatus wifiStatus = WifiStatus::DISCONNECTED;
    WifiEventCallback wifiCallback;
    esp_event_handler_instance_t wifiHandlerInstance;
    esp_event_handler_instance_t ipHandlerInstance;
    TimerHandle_t networkTimeoutTimer;
    bool networkConnected;
    
    // Static members for timeout handling
    static QueueHandle_t timeoutQueue;
    static TaskHandle_t timeoutTaskHandle;
};
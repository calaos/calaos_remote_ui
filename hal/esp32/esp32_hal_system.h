#pragma once

#include "../hal_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <atomic>

class Esp32HalSystem : public HalSystem
{
public:
    HalResult init() override;
    HalResult deinit() override;
    void delay(uint32_t ms) override;
    uint64_t getTimeMs() override;
    void restart() override;
    std::string getDeviceInfo() const override;
    HalResult saveConfig(const std::string& key, const std::string& value) override;
    HalResult loadConfig(const std::string& key, std::string& value) override;
    HalResult eraseConfig(const std::string& key) override;

    // NTP time synchronization
    HalResult initNtp() override;
    HalResult waitForTimeSync(uint32_t timeoutMs = 15000) override;
    bool isTimeSynced() const override;
    void startNtpRetryTimer() override;
    void stopNtpRetryTimer() override;

    // Called from SNTP sync callback
    void onNtpSyncComplete();

private:
    bool nvsInitialized = false;

    // NTP synchronization
    std::atomic<bool> ntpSynced{false};
    bool ntpInitialized = false;
    SemaphoreHandle_t ntpSyncSemaphore = nullptr;
    esp_timer_handle_t ntpRetryTimer = nullptr;

    static void ntpRetryTimerCallback(void* arg);
};
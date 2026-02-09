/**
 * @file esp32_ota.h
 * @brief ESP32 OTA implementation using ESP-IDF OTA APIs
 */

#pragma once

#include "../hal_ota.h"
#include <atomic>
#include <mutex>
#include <map>

class Esp32Ota : public HalOta
{
public:
    Esp32Ota();
    ~Esp32Ota() override;

    HalResult startUpdate(const std::string& url,
                           const std::string& expectedChecksum,
                           const std::map<std::string, std::string>& authHeaders,
                           HalOtaProgressCallback progressCallback) override;

    HalResult abortUpdate() override;
    HalOtaProgress getProgress() const override;
    HalOtaState getState() const override;
    bool isSupported() const override;
    HalResult markFirmwareValid() override;

private:
    void otaTask();
    void updateProgress(HalOtaState state, int percent, size_t downloaded, size_t total, const std::string& error = "");

    std::string updateUrl;
    std::string expectedChecksum;
    std::map<std::string, std::string> authHeaders_;
    HalOtaProgressCallback callback;

    mutable std::mutex progressMutex;
    HalOtaProgress currentProgress;

    std::atomic<bool> abortRequested{false};
    std::atomic<bool> updateInProgress{false};
};

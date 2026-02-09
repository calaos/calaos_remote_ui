/**
 * @file linux_ota.h
 * @brief Linux OTA stub implementation
 */

#pragma once

#include "../hal_ota.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <map>

/**
 * @brief Linux OTA stub - simulates OTA for development
 *
 * For "none" backend: logs only, no actual update
 * For "luckfox" backend: TODO - implement swupdate/rauc integration
 */
class LinuxOta : public HalOta
{
public:
    LinuxOta();
    ~LinuxOta() override;

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
    void simulateOta();
    void updateProgress(HalOtaState state, int percent, size_t downloaded, size_t total, const std::string& error = "");

    std::string updateUrl;
    std::string expectedChecksum;
    std::map<std::string, std::string> authHeaders_;
    HalOtaProgressCallback callback;

    mutable std::mutex progressMutex;
    HalOtaProgress currentProgress;

    std::atomic<bool> abortRequested{false};
    std::atomic<bool> updateInProgress{false};
    std::thread otaThread;
};

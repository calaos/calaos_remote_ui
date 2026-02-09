/**
 * @file linux_ota.cpp
 * @brief Linux OTA stub implementation
 */

#include "linux_ota.h"
#include "logging.h"
#include "../hal.h"
#include "board_config.h"

#include <chrono>

static const char* TAG = "hal.ota";

LinuxOta::LinuxOta()
{
    ESP_LOGI(TAG, "Linux OTA backend initialized (backend: %s)", BOARD_OTA_BACKEND);
}

LinuxOta::~LinuxOta()
{
    if (updateInProgress)
    {
        abortRequested = true;
        if (otaThread.joinable())
        {
            otaThread.join();
        }
    }
}

HalResult LinuxOta::startUpdate(const std::string& url,
                                 const std::string& checksum,
                                 const std::map<std::string, std::string>& authHeaders,
                                 HalOtaProgressCallback progressCallback)
{
    if (updateInProgress)
    {
        ESP_LOGE(TAG, "OTA update already in progress");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", url.c_str());

    // Check OTA backend type
    std::string backend = BOARD_OTA_BACKEND;

    if (backend == "none")
    {
        ESP_LOGW(TAG, "OTA backend is 'none' - simulating update for development");
    }
    else if (backend == "luckfox")
    {
        // TODO: Implement actual Luckfox OTA using swupdate or rauc
        ESP_LOGW(TAG, "Luckfox OTA backend not yet implemented - simulating");
    }

    updateUrl = url;
    expectedChecksum = checksum;
    authHeaders_ = authHeaders;
    callback = progressCallback;
    abortRequested = false;
    updateInProgress = true;

    updateProgress(HalOtaState::Downloading, 0, 0, 0);

    // Start simulation thread
    if (otaThread.joinable())
    {
        otaThread.join();
    }
    otaThread = std::thread(&LinuxOta::simulateOta, this);

    return HalResult::OK;
}

HalResult LinuxOta::abortUpdate()
{
    if (!updateInProgress)
    {
        return HalResult::ERROR;
    }

    ESP_LOGW(TAG, "Aborting OTA update");
    abortRequested = true;
    return HalResult::OK;
}

HalOtaProgress LinuxOta::getProgress() const
{
    std::lock_guard<std::mutex> lock(progressMutex);
    return currentProgress;
}

HalOtaState LinuxOta::getState() const
{
    std::lock_guard<std::mutex> lock(progressMutex);
    return currentProgress.state;
}

bool LinuxOta::isSupported() const
{
    std::string backend = BOARD_OTA_BACKEND;

    // "none" backend is supported (for testing) but won't do anything
    // "luckfox" backend will be supported once implemented
    return (backend == "none" || backend == "luckfox");
}

HalResult LinuxOta::markFirmwareValid()
{
    std::string backend = BOARD_OTA_BACKEND;

    if (backend == "luckfox")
    {
        // TODO: Implement for Luckfox (e.g., mark partition as bootable in swupdate)
        ESP_LOGW(TAG, "markFirmwareValid: Luckfox implementation TODO");
    }
    else
    {
        ESP_LOGI(TAG, "markFirmwareValid: no-op for '%s' backend", backend.c_str());
    }

    return HalResult::OK;
}

void LinuxOta::updateProgress(HalOtaState state, int percent, size_t downloaded, size_t total, const std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        currentProgress.state = state;
        currentProgress.percent = percent;
        currentProgress.bytesDownloaded = downloaded;
        currentProgress.totalBytes = total;
        currentProgress.errorMessage = error;
    }

    if (callback)
    {
        callback(currentProgress);
    }
}

void LinuxOta::simulateOta()
{
    ESP_LOGI(TAG, "Simulating OTA update for development");

    const size_t totalBytes = 10 * 1024 * 1024;  // Simulate 10MB firmware
    const int steps = 20;
    const int delayPerStep = 200;  // ms

    for (int i = 1; i <= steps && !abortRequested; ++i)
    {
        int percent = (i * 100) / steps;
        size_t downloaded = (totalBytes * i) / steps;

        ESP_LOGI(TAG, "OTA progress: %d%%", percent);
        updateProgress(HalOtaState::Downloading, percent, downloaded, totalBytes);

        std::this_thread::sleep_for(std::chrono::milliseconds(delayPerStep));
    }

    if (abortRequested)
    {
        ESP_LOGW(TAG, "OTA simulation aborted");
        updateProgress(HalOtaState::Error, 0, 0, 0, "Update aborted");
        updateInProgress = false;
        return;
    }

    // Simulate installation
    ESP_LOGI(TAG, "Simulating installation...");
    updateProgress(HalOtaState::Installing, 100, totalBytes, totalBytes);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string backend = BOARD_OTA_BACKEND;

    if (backend == "luckfox")
    {
        // TODO: Actually perform OTA for Luckfox
        // - Download firmware to temp location
        // - Verify checksum
        // - Use swupdate/rauc to install
        // - Reboot
        ESP_LOGW(TAG, "Luckfox OTA: would reboot here (not implemented)");
        updateProgress(HalOtaState::Rebooting, 100, totalBytes, totalBytes);
    }
    else
    {
        ESP_LOGI(TAG, "OTA simulation complete (no actual update for 'none' backend)");
        updateProgress(HalOtaState::Idle, 0, 0, 0);
    }

    updateInProgress = false;
}

// Factory function implementation for Linux
std::unique_ptr<HalOta> createOta()
{
    return std::make_unique<LinuxOta>();
}

/**
 * @file ota_manager.h
 * @brief OTA update manager - orchestrates firmware updates
 */

#pragma once

#include "../flux/flux.h"
#include "../hal/hal_ota.h"
#include <memory>
#include <string>
#include <map>

/**
 * @brief Manages OTA firmware updates
 *
 * Subscribes to OtaUpdateAvailable events and automatically
 * triggers the download and installation process using the HAL OTA backend.
 */
class OtaManager
{
public:
    static OtaManager& getInstance();

    /**
     * @brief Initialize the OTA manager
     * @return true if successful
     */
    bool init();

    /**
     * @brief Start an OTA update manually
     * @param url Download URL
     * @param checksum SHA256 checksum
     * @param version Version string for display
     * @return true if update started
     */
    bool startUpdate(const std::string& url, const std::string& checksum, const std::string& version);

    /**
     * @brief Abort ongoing OTA update
     * @return true if aborted
     */
    bool abortUpdate();

    /**
     * @brief Check if OTA is in progress
     */
    bool isUpdateInProgress() const;

    /**
     * @brief Check if OTA is supported on this platform
     */
    bool isSupported() const;

private:
    OtaManager();
    ~OtaManager();

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    void onStateChanged(const AppState& state);
    void onOtaProgress(const HalOtaProgress& progress);
    std::map<std::string, std::string> buildAuthHeaders();

    std::unique_ptr<HalOta> ota_;
    SubscriptionId subscriptionId_ = 0;
    bool initialized_ = false;
    bool updateInProgress_ = false;
    std::string currentVersion_;
};

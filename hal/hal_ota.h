/**
 * @file hal_ota.h
 * @brief Hardware Abstraction Layer for OTA (Over-The-Air) firmware updates
 */

#pragma once

#include "hal_types.h"
#include <string>
#include <functional>
#include <cstdint>
#include <memory>
#include <map>

/**
 * @brief HAL OTA update state
 */
enum class HalOtaState
{
    Idle,           // No OTA in progress
    Downloading,    // Downloading firmware
    Installing,     // Writing firmware to flash
    Rebooting,      // About to reboot
    Error           // Error occurred
};

/**
 * @brief OTA progress information from HAL
 */
struct HalOtaProgress
{
    HalOtaState state = HalOtaState::Idle;
    int percent = 0;                     // 0-100 progress
    size_t bytesDownloaded = 0;
    size_t totalBytes = 0;
    std::string errorMessage;
};

/**
 * @brief Callback type for OTA progress updates
 */
using HalOtaProgressCallback = std::function<void(const HalOtaProgress&)>;

/**
 * @brief Abstract interface for OTA operations
 */
class HalOta
{
public:
    virtual ~HalOta() = default;

    /**
     * @brief Start OTA update from the given URL
     * @param url URL to download firmware from
     * @param expectedChecksum SHA256 checksum to verify (empty to skip verification)
     * @param authHeaders HTTP headers for authentication
     * @param progressCallback Callback for progress updates
     * @return HalResult::OK if update started, error otherwise
     */
    virtual HalResult startUpdate(const std::string& url,
                                   const std::string& expectedChecksum,
                                   const std::map<std::string, std::string>& authHeaders,
                                   HalOtaProgressCallback progressCallback) = 0;

    /**
     * @brief Abort ongoing OTA update
     * @return HalResult::OK if aborted, error if no update in progress
     */
    virtual HalResult abortUpdate() = 0;

    /**
     * @brief Get current OTA progress
     * @return Current progress information
     */
    virtual HalOtaProgress getProgress() const = 0;

    /**
     * @brief Get current OTA state
     * @return Current state
     */
    virtual HalOtaState getState() const = 0;

    /**
     * @brief Check if OTA is supported on this platform
     * @return true if OTA is supported
     */
    virtual bool isSupported() const = 0;

    /**
     * @brief Mark current firmware as valid (for rollback protection)
     * Call this after successful boot to prevent automatic rollback.
     * @return HalResult::OK on success
     */
    virtual HalResult markFirmwareValid() = 0;
};

/**
 * @brief Factory function to create platform-specific OTA implementation
 * @return Unique pointer to HalOta implementation
 */
std::unique_ptr<HalOta> createOta();

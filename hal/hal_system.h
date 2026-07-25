#pragma once

#include "hal_types.h"
#include <string>
#include <cstdint>
#include "board_config.h"

class DeviceConfig;
struct CalaosConfig;

class HalSystem
{
public:
    virtual ~HalSystem() = default;

    virtual HalResult init() = 0;
    virtual HalResult deinit() = 0;
    virtual void delay(uint32_t ms) = 0;
    virtual uint64_t getTimeMs() = 0;
    virtual void restart() = 0;
    virtual std::string getDeviceInfo() const = 0;
    virtual HalResult saveConfig(const std::string& key, const std::string& value) = 0;
    virtual HalResult loadConfig(const std::string& key, std::string& value) = 0;
    virtual HalResult eraseConfig(const std::string& key) = 0;

    std::string getPlatform() const { return BOARD_PLATFORM; }
    std::string getManufacturer() const { return BOARD_MANUFACTURER; }
    std::string getHardwareId() const { return BOARD_HARDWARE_ID; }
    bool hasWifi() const { return BOARD_HAS_WIFI; }
    bool hasEthernet() const { return BOARD_HAS_ETHERNET; }
    bool hasTouchscreen() const { return BOARD_HAS_TOUCHSCREEN; }

    // Load device provisioning config from platform storage and parse it.
    // On ESP32: reads from dedicated raw partition "config".
    // On Linux: reads from BOARD_DEVICE_CONFIG_PATH or config dir fallback.
    // Returns HalResult::OK on success, HalResult::ERROR on failure.
    virtual HalResult loadDeviceConfig(DeviceConfig &devCfg) = 0;

    // Persist a device provisioning config to platform storage.
    // On ESP32: serializes and writes to the dedicated raw partition "config"
    //           (erase + write + read-back verification).
    // On Linux: writes to BOARD_DEVICE_CONFIG_PATH or the config dir fallback.
    // Returns HalResult::OK on success, HalResult::ERROR on failure.
    virtual HalResult saveDeviceConfig(const CalaosConfig &cfg) = 0;

    // NTP time synchronization
    // Initialize NTP client with multiple servers
    virtual HalResult initNtp() = 0;
    // Blocking wait for NTP time synchronization
    // Returns HalResult::OK on success, HalResult::TIMEOUT on timeout
    virtual HalResult waitForTimeSync(uint32_t timeoutMs = 15000) = 0;
    // Check if time has been synchronized via NTP
    virtual bool isTimeSynced() const = 0;
    // Start background retry timer (30s interval) for failed NTP sync
    virtual void startNtpRetryTimer() = 0;
    // Stop NTP retry timer
    virtual void stopNtpRetryTimer() = 0;
};
#pragma once

#include "hal_types.h"
#include <string>

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
    virtual std::string getFirmwareVersion() const = 0;
    virtual HalResult saveConfig(const std::string& key, const std::string& value) = 0;
    virtual HalResult loadConfig(const std::string& key, std::string& value) = 0;
    virtual HalResult eraseConfig(const std::string& key) = 0;
};
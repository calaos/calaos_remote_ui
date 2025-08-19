#pragma once

#include "../hal_system.h"

class Esp32HalSystem : public HalSystem
{
public:
    HalResult init() override;
    HalResult deinit() override;
    void delay(uint32_t ms) override;
    uint64_t getTimeMs() override;
    void restart() override;
    std::string getDeviceInfo() const override;
    std::string getFirmwareVersion() const override;
    HalResult saveConfig(const std::string& key, const std::string& value) override;
    HalResult loadConfig(const std::string& key, std::string& value) override;
    HalResult eraseConfig(const std::string& key) override;

private:
    bool nvsInitialized = false;
};
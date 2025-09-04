#pragma once

#include "../hal_system.h"
#include <map>
#include <string>
#include <filesystem>

class LinuxHalSystem : public HalSystem {
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
    std::string getConfigDirPath() const;
    std::string sanitizeFilename(const std::string& filename) const;
    std::string getConfigFilePath(const std::string& key) const;
    HalResult ensureConfigDir();
    
    std::string config_dir_path_;
};
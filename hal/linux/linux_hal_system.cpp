#include "linux_hal_system.h"
#include "logging.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>

static const char* TAG = "hal.system";

HalResult LinuxHalSystem::init()
{
    ESP_LOGI(TAG, "Initializing Linux system");

    config_file_path_ = getConfigFilePath();
    loadConfigFile();

    ESP_LOGI(TAG, "Linux system initialized");
    return HalResult::OK;
}

HalResult LinuxHalSystem::deinit()
{
    saveConfigFile();
    config_data_.clear();
    return HalResult::OK;
}

void LinuxHalSystem::delay(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

uint64_t LinuxHalSystem::getTimeMs()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void LinuxHalSystem::restart()
{
    ESP_LOGI(TAG, "System restart requested");
    saveConfigFile();

    // In a real embedded Linux system, you might use:
    // system("reboot");
    // For development, we just exit
    exit(0);
}

std::string LinuxHalSystem::getDeviceInfo() const
{
    struct utsname buffer;
    if (uname(&buffer) != 0)
        return "Unknown Linux System";

    std::string info = std::string(buffer.sysname) + " " +
                      std::string(buffer.release) + " " +
                      std::string(buffer.machine);

    // Try to get more specific hardware info
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open())
    {
        std::string line;
        while (std::getline(cpuinfo, line))
        {
            if (line.find("model name") != std::string::npos)
            {
                size_t pos = line.find(":");
                if (pos != std::string::npos)
                {
                    info += " - " + line.substr(pos + 2);
                    break;
                }
            }
        }
    }

    return info;
}

std::string LinuxHalSystem::getFirmwareVersion() const
{
    return "Calaos Remote UI Linux v1.0.0";
}

HalResult LinuxHalSystem::saveConfig(const std::string& key, const std::string& value)
{
    config_data_[key] = value;
    return saveConfigFile();
}

HalResult LinuxHalSystem::loadConfig(const std::string& key, std::string& value)
{
    auto it = config_data_.find(key);
    if (it != config_data_.end())
    {
        value = it->second;
        return HalResult::OK;
    }
    return HalResult::ERROR;
}

HalResult LinuxHalSystem::eraseConfig(const std::string& key)
{
    config_data_.erase(key);
    return saveConfigFile();
}

std::string LinuxHalSystem::getConfigFilePath() const
{
    const char* home = getenv("HOME");
    if (home)
        return std::string(home) + "/.calaos_remote_ui_config";

    return "/tmp/calaos_remote_ui_config";
}

HalResult LinuxHalSystem::loadConfigFile()
{
    std::ifstream file(config_file_path_);
    if (!file.is_open())
    {
        // File doesn't exist yet, that's okay
        return HalResult::OK;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t pos = line.find('=');
        if (pos != std::string::npos)
        {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            config_data_[key] = value;
        }
    }

    return HalResult::OK;
}

HalResult LinuxHalSystem::saveConfigFile()
{
    std::ofstream file(config_file_path_);
    if (!file.is_open())
    {
        ESP_LOGE(TAG, "Failed to open config file for writing: %s", config_file_path_.c_str());
        return HalResult::ERROR;
    }

    for (const auto& pair : config_data_)
        file << pair.first << "=" << pair.second << std::endl;

    return HalResult::OK;
}
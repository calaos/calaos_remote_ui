#include "linux_hal_system.h"
#include "logging.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>
#include <filesystem>
#include <sys/stat.h>

static const char* TAG = "hal.system";

HalResult LinuxHalSystem::init()
{
    ESP_LOGI(TAG, "Initializing Linux system");

    config_dir_path_ = getConfigDirPath();
    if (ensureConfigDir() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to create config directory");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Linux system initialized with config dir: %s", config_dir_path_.c_str());
    return HalResult::OK;
}

HalResult LinuxHalSystem::deinit()
{
    ESP_LOGI(TAG, "Deinitializing Linux system");
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

HalResult LinuxHalSystem::saveConfig(const std::string& key, const std::string& value)
{
    std::string filePath = getConfigFilePath(key);

    std::ofstream file(filePath);
    if (!file.is_open())
    {
        ESP_LOGE(TAG, "Failed to open config file for writing: %s", filePath.c_str());
        return HalResult::ERROR;
    }

    file << value;
    file.close();

    ESP_LOGD(TAG, "Saved config key '%s' to file: %s", key.c_str(), filePath.c_str());
    return HalResult::OK;
}

HalResult LinuxHalSystem::loadConfig(const std::string& key, std::string& value)
{
    std::string filePath = getConfigFilePath(key);

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        ESP_LOGD(TAG, "Config file not found: %s", filePath.c_str());
        return HalResult::ERROR;
    }

    // Read entire file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    value = buffer.str();

    ESP_LOGD(TAG, "Loaded config key '%s' from file: %s", key.c_str(), filePath.c_str());
    return HalResult::OK;
}

HalResult LinuxHalSystem::eraseConfig(const std::string& key)
{
    std::string filePath = getConfigFilePath(key);

    if (std::filesystem::exists(filePath))
    {
        std::error_code ec;
        if (!std::filesystem::remove(filePath, ec))
        {
            ESP_LOGE(TAG, "Failed to remove config file: %s - %s", filePath.c_str(), ec.message().c_str());
            return HalResult::ERROR;
        }
        ESP_LOGD(TAG, "Erased config key '%s' (removed file: %s)", key.c_str(), filePath.c_str());
    }

    return HalResult::OK;
}

std::string LinuxHalSystem::getConfigDirPath() const
{
    // Priority: environment variable > $HOME/.config/calaos_remote_ui > /tmp/calaos_remote_ui
    const char* configPath = getenv("CALAOS_UI_CONFIG_PATH");
    if (configPath)
        return std::string(configPath);

    const char* home = getenv("HOME");
    if (home)
        return std::string(home) + "/.config/calaos_remote_ui";

    return "/tmp/calaos_remote_ui";
}

std::string LinuxHalSystem::sanitizeFilename(const std::string& filename) const
{
    std::string sanitized = filename;

    // Replace invalid filesystem characters with underscores
    for (char& c : sanitized)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '\0')
        {
            c = '_';
        }
    }

    // Ensure the filename is not empty and doesn't start with a dot
    if (sanitized.empty() || sanitized[0] == '.')
        sanitized = "_" + sanitized;

    return sanitized;
}

std::string LinuxHalSystem::getConfigFilePath(const std::string& key) const
{
    std::string sanitizedKey = sanitizeFilename(key);
    return config_dir_path_ + "/" + sanitizedKey;
}

HalResult LinuxHalSystem::ensureConfigDir()
{
    std::error_code ec;

    if (!std::filesystem::exists(config_dir_path_, ec))
    {
        if (!std::filesystem::create_directories(config_dir_path_, ec))
        {
            ESP_LOGE(TAG, "Failed to create config directory '%s': %s",
                     config_dir_path_.c_str(), ec.message().c_str());
            return HalResult::ERROR;
        }
        ESP_LOGI(TAG, "Created config directory: %s", config_dir_path_.c_str());
    }

    return HalResult::OK;
}
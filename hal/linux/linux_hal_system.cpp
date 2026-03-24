#include "linux_hal_system.h"
#include "logging.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/utsname.h>
#include <filesystem>
#include <sys/stat.h>
#include "flux.h"
#include "calaos_config/device_config.h"

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
    return getManufacturer() + " " + getHardwareId() + " (" + getPlatform() + ")";
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

// ============================================================================
// Device Provisioning Config
// ============================================================================

HalResult LinuxHalSystem::loadDeviceConfig(DeviceConfig &devCfg)
{
    // Determine the config source path.
    // If BOARD_DEVICE_CONFIG_PATH is set (e.g. /dev/mmcblk0p3), read from that
    // raw partition/block device. Otherwise fall back to a regular file in the
    // user config directory (development mode).
    std::string configPath(BOARD_DEVICE_CONFIG_PATH);
    if (configPath.empty())
        configPath = config_dir_path_ + "/device_config.bin";

    // Local streaming reader that wraps std::ifstream
    class FileCfgReader : public CfgReader
    {
    public:
        explicit FileCfgReader(std::ifstream &f) : f_(f) {}

        bool read(uint8_t *dst, size_t len) override
        {
            return static_cast<bool>(f_.read(reinterpret_cast<char *>(dst), len));
        }

    private:
        std::ifstream &f_;
    };

    std::ifstream file(configPath, std::ios::binary);
    if (!file.is_open())
    {
        ESP_LOGD(TAG, "Device config not found: %s", configPath.c_str());
        return HalResult::ERROR;
    }

    FileCfgReader reader(file);
    devCfg.loadFromReader(reader);

    if (devCfg.isLoaded())
        ESP_LOGI(TAG, "Device config loaded from %s", configPath.c_str());

    return devCfg.isLoaded() ? HalResult::OK : HalResult::ERROR;
}

// ============================================================================
// NTP Time Synchronization (simulated on Linux - system handles NTP)
// ============================================================================

HalResult LinuxHalSystem::initNtp()
{
    // Linux system typically has NTP handled by systemd-timesyncd or ntpd
    ESP_LOGI(TAG, "NTP init (simulated on Linux - system handles time sync)");

    // Simulate async NTP sync like ESP32: spawn thread that fires event after short delay
    std::thread([]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ESP_LOGI(TAG, "NTP time synchronized (simulated)");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpTimeSynced));
    }).detach();

    return HalResult::OK;
}

HalResult LinuxHalSystem::waitForTimeSync(uint32_t timeoutMs)
{
    // Linux system time is assumed to be already synchronized
    (void)timeoutMs;
    ESP_LOGI(TAG, "NTP wait (no-op on Linux - time assumed synced)");
    return HalResult::OK;
}

bool LinuxHalSystem::isTimeSynced() const
{
    // Always return true on Linux - system handles time sync
    return true;
}

void LinuxHalSystem::startNtpRetryTimer()
{
    // No-op on Linux
}

void LinuxHalSystem::stopNtpRetryTimer()
{
    // No-op on Linux
}
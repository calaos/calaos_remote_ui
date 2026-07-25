#include "esp32_hal_system.h"
#include "logging.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "esp_partition.h"
#include "flux.h"
#include "board_config.h"
#include "calaos_config/device_config.h"

static const char* TAG = "hal.system";
static const char* NVS_NAMESPACE = "calaos_config";

// Global pointer for SNTP callback (C callback cannot use member function directly)
static Esp32HalSystem* g_systemInstance = nullptr;

// NTP sync interval: 1 hour (in milliseconds)
static const uint32_t NTP_SYNC_INTERVAL_MS = 3600 * 1000;

// SNTP time sync notification callback
static void ntpSyncNotificationCb(struct timeval* tv)
{
    ESP_LOGI(TAG, "NTP time synchronized: %lld.%06ld", (long long)tv->tv_sec, tv->tv_usec);
    if (g_systemInstance)
    {
        g_systemInstance->onNtpSyncComplete();
    }
}

HalResult Esp32HalSystem::init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK)
    {
        nvsInitialized = true;
        ESP_LOGI(TAG, "System initialized");
        return HalResult::OK;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }
}

HalResult Esp32HalSystem::deinit()
{
    nvsInitialized = false;
    return HalResult::OK;
}

void Esp32HalSystem::delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint64_t Esp32HalSystem::getTimeMs()
{
    return esp_timer_get_time() / 1000;
}

void Esp32HalSystem::restart()
{
    esp_restart();
}

std::string Esp32HalSystem::getDeviceInfo() const
{
    return getManufacturer() + " " + getHardwareId() + " (" + getPlatform() + ")";
}

HalResult Esp32HalSystem::saveConfig(const std::string& key, const std::string& value)
{
    if (!nvsInitialized)
        return HalResult::ERROR;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = nvs_set_str(handle, key.c_str(), value.c_str());
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save config %s: %s", key.c_str(), esp_err_to_name(ret));
        nvs_close(handle);
        return HalResult::ERROR;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalSystem::loadConfig(const std::string& key, std::string& value)
{
    if (!nvsInitialized)
        return HalResult::ERROR;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    size_t requiredSize = 0;
    ret = nvs_get_str(handle, key.c_str(), nullptr, &requiredSize);
    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return HalResult::ERROR;
    }

    char* buffer = new char[requiredSize];
    ret = nvs_get_str(handle, key.c_str(), buffer, &requiredSize);
    if (ret == ESP_OK)
        value = std::string(buffer);

    delete[] buffer;
    nvs_close(handle);

    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalSystem::eraseConfig(const std::string& key)
{
    if (!nvsInitialized)
        return HalResult::ERROR;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = nvs_erase_key(handle, key.c_str());
    if (ret == ESP_OK)
        nvs_commit(handle);

    nvs_close(handle);

    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

// ============================================================================
// Device Provisioning Config
// ============================================================================

HalResult Esp32HalSystem::loadDeviceConfig(DeviceConfig &devCfg)
{
    // Find the "config" partition (type data, subtype 0x40)
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "config");

    if (!partition)
    {
        ESP_LOGW(TAG, "Device config partition 'config' not found");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Found device config partition: offset=0x%lx, size=%lu",
             (unsigned long)partition->address, (unsigned long)partition->size);

    // Local streaming reader that wraps esp_partition_read()
    class PartitionCfgReader : public CfgReader
    {
    public:
        PartitionCfgReader(const esp_partition_t *part) : part_(part) {}

        bool read(uint8_t *dst, size_t len) override
        {
            if (offset_ + len > part_->size)
                return false;
            esp_err_t ret = esp_partition_read(part_, offset_, dst, len);
            if (ret != ESP_OK)
                return false;
            offset_ += len;
            return true;
        }

    private:
        const esp_partition_t *part_;
        size_t offset_ = 0;
    };

    PartitionCfgReader reader(partition);
    devCfg.loadFromReader(reader);

    return devCfg.isLoaded() ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalSystem::saveDeviceConfig(const CalaosConfig &cfg)
{
    std::vector<uint8_t> blob;
    if (!calaosConfigSerialize(cfg, blob))
    {
        ESP_LOGE(TAG, "Failed to serialize device config");
        return HalResult::ERROR;
    }

    // Find the "config" partition (type data, subtype 0x40)
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "config");

    if (!partition)
    {
        ESP_LOGE(TAG, "Device config partition 'config' not found");
        return HalResult::ERROR;
    }

    if (blob.size() > partition->size)
    {
        ESP_LOGE(TAG, "Serialized config (%zu bytes) larger than partition (%lu bytes)",
                 blob.size(), (unsigned long)partition->size);
        return HalResult::ERROR;
    }

    // Erase the needed range, rounded up to the partition erase block size
    size_t eraseBlock = partition->erase_size ? partition->erase_size : 4096;
    size_t eraseLen = ((blob.size() + eraseBlock - 1) / eraseBlock) * eraseBlock;
    if (eraseLen > partition->size)
        eraseLen = partition->size;

    ESP_LOGI(TAG, "Writing device config: %zu bytes to partition at 0x%lx (erasing %zu bytes)",
             blob.size(), (unsigned long)partition->address, eraseLen);

    esp_err_t ret = esp_partition_erase_range(partition, 0, eraseLen);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to erase config partition: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_partition_write(partition, 0, blob.data(), blob.size());
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write config partition: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    // Read-back verification: the flash content must match the blob byte-exactly
    std::vector<uint8_t> readBack(blob.size());
    ret = esp_partition_read(partition, 0, readBack.data(), readBack.size());
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read back config partition: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    if (readBack != blob)
    {
        ESP_LOGE(TAG, "Config partition read-back verification failed (content mismatch)");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Device config written and verified successfully");
    return HalResult::OK;
}

// ============================================================================
// NTP Time Synchronization
// ============================================================================

HalResult Esp32HalSystem::initNtp()
{
    if (ntpInitialized)
    {
        ESP_LOGD(TAG, "NTP already initialized");
        return HalResult::OK;
    }

    ESP_LOGI(TAG, "Initializing NTP client");

    // Store global pointer for C callback
    g_systemInstance = this;

    // Create semaphore for blocking wait
    if (ntpSyncSemaphore == nullptr)
    {
        ntpSyncSemaphore = xSemaphoreCreateBinary();
        if (ntpSyncSemaphore == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create NTP sync semaphore");
            return HalResult::ERROR;
        }
    }

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Set sync interval to 1 hour to prevent clock drift
    // SNTP will automatically re-sync at this interval
    esp_sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);

    // Configure multiple NTP servers for redundancy
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");

    // Set sync notification callback (called on every sync, including periodic)
    sntp_set_time_sync_notification_cb(ntpSyncNotificationCb);

    // Set timezone to UTC (can be changed if needed)
    setenv("TZ", "UTC0", 1);
    tzset();

    // Initialize SNTP
    esp_sntp_init();

    ntpInitialized = true;
    ESP_LOGI(TAG, "NTP client initialized with servers: pool.ntp.org, time.google.com, time.cloudflare.com");
    ESP_LOGI(TAG, "NTP periodic resync interval: %lu ms (%.1f hours)", NTP_SYNC_INTERVAL_MS, NTP_SYNC_INTERVAL_MS / 3600000.0f);

    return HalResult::OK;
}

HalResult Esp32HalSystem::waitForTimeSync(uint32_t timeoutMs)
{
    if (!ntpInitialized)
    {
        ESP_LOGE(TAG, "NTP not initialized, call initNtp() first");
        return HalResult::ERROR;
    }

    // Already synced?
    if (ntpSynced.load())
    {
        ESP_LOGD(TAG, "Time already synchronized");
        return HalResult::OK;
    }

    ESP_LOGI(TAG, "Waiting for NTP time sync (timeout: %lu ms)", timeoutMs);

    // Wait for semaphore with timeout
    if (xSemaphoreTake(ntpSyncSemaphore, pdMS_TO_TICKS(timeoutMs)) == pdTRUE)
    {
        ESP_LOGI(TAG, "NTP time synchronization successful");
        return HalResult::OK;
    }
    else
    {
        ESP_LOGW(TAG, "NTP time synchronization timeout after %lu ms", timeoutMs);
        return HalResult::TIMEOUT;
    }
}

bool Esp32HalSystem::isTimeSynced() const
{
    return ntpSynced.load();
}

void Esp32HalSystem::onNtpSyncComplete()
{
    bool wasAlreadySynced = ntpSynced.load();
    ntpSynced.store(true);

    // Give semaphore to unblock waitForTimeSync() (only for initial sync)
    if (!wasAlreadySynced && ntpSyncSemaphore)
    {
        xSemaphoreGive(ntpSyncSemaphore);
    }

    // Stop retry timer if running
    stopNtpRetryTimer();

    // Dispatch NTP synced event only for initial sync
    // Periodic resyncs don't need UI notification
    if (!wasAlreadySynced)
    {
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpTimeSynced));
    }
    else
    {
        ESP_LOGI(TAG, "Periodic NTP resync completed");
    }

    // Log current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Current time: %s UTC", strftime_buf);
}

void Esp32HalSystem::startNtpRetryTimer()
{
    if (ntpRetryTimer != nullptr)
    {
        ESP_LOGD(TAG, "NTP retry timer already running");
        return;
    }

    ESP_LOGI(TAG, "Starting NTP retry timer (30s interval)");

    esp_timer_create_args_t timerArgs = {
        .callback = ntpRetryTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ntp_retry",
        .skip_unhandled_events = false
    };

    esp_err_t ret = esp_timer_create(&timerArgs, &ntpRetryTimer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create NTP retry timer: %s", esp_err_to_name(ret));
        return;
    }

    // Start periodic timer (30 seconds)
    ret = esp_timer_start_periodic(ntpRetryTimer, 30 * 1000 * 1000);  // microseconds
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start NTP retry timer: %s", esp_err_to_name(ret));
        esp_timer_delete(ntpRetryTimer);
        ntpRetryTimer = nullptr;
    }
}

void Esp32HalSystem::stopNtpRetryTimer()
{
    if (ntpRetryTimer == nullptr)
        return;

    ESP_LOGD(TAG, "Stopping NTP retry timer");
    esp_timer_stop(ntpRetryTimer);
    esp_timer_delete(ntpRetryTimer);
    ntpRetryTimer = nullptr;
}

void Esp32HalSystem::ntpRetryTimerCallback(void* arg)
{
    Esp32HalSystem* self = static_cast<Esp32HalSystem*>(arg);
    if (self && !self->isTimeSynced())
    {
        ESP_LOGI(TAG, "NTP retry timer fired, dispatching NtpSyncStarted event");
        // Dispatch event to notify UI that we're retrying
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpSyncStarted));

        // SNTP will automatically retry, we just notify UI
        // Check if SNTP status indicates sync in progress
        sntp_sync_status_t status = sntp_get_sync_status();
        if (status == SNTP_SYNC_STATUS_COMPLETED)
        {
            // Sync completed between checks
            self->onNtpSyncComplete();
        }
    }
}
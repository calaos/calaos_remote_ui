/**
 * @file esp32_ota.cpp
 * @brief ESP32 OTA implementation using ESP-IDF OTA APIs
 */

#include "esp32_ota.h"
#include "../logging.h"
#include "../hal.h"
#include "board_config.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "ota_download.h"
#include "esp_heap_caps.h"

#include <cstring>
#include <thread>
#include <string>

static const char* TAG = "hal.ota";

Esp32Ota::Esp32Ota()
{
    ESP_LOGI(TAG, "ESP32 OTA backend initialized");
}

Esp32Ota::~Esp32Ota()
{
    if (updateInProgress)
    {
        abortRequested = true;
        // Wait a bit for the task to notice
        HAL::getInstance().getSystem().delay(100);
    }
}

HalResult Esp32Ota::startUpdate(const std::string& url,
                                 const std::string& checksum,
                                 const std::map<std::string, std::string>& authHeaders,
                                 HalOtaProgressCallback progressCallback)
{
    if (updateInProgress)
    {
        ESP_LOGE(TAG, "OTA update already in progress");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", url.c_str());

    updateUrl = url;
    expectedChecksum = checksum;
    authHeaders_ = authHeaders;
    callback = progressCallback;
    abortRequested = false;
    updateInProgress = true;

    updateProgress(HalOtaState::Downloading, 0, 0, 0);

    // Start OTA in a separate thread
    std::thread otaThread(&Esp32Ota::otaTask, this);
    otaThread.detach();

    return HalResult::OK;
}

HalResult Esp32Ota::abortUpdate()
{
    if (!updateInProgress)
    {
        return HalResult::ERROR;
    }

    ESP_LOGW(TAG, "Aborting OTA update");
    abortRequested = true;
    return HalResult::OK;
}

HalOtaProgress Esp32Ota::getProgress() const
{
    std::lock_guard<std::mutex> lock(progressMutex);
    return currentProgress;
}

HalOtaState Esp32Ota::getState() const
{
    std::lock_guard<std::mutex> lock(progressMutex);
    return currentProgress.state;
}

bool Esp32Ota::isSupported() const
{
    return true;
}

HalResult Esp32Ota::markFirmwareValid()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            ESP_LOGI(TAG, "Marking firmware as valid (canceling rollback)");
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(err));
                return HalResult::ERROR;
            }
            return HalResult::OK;
        }
    }

    // Firmware already marked valid or not in OTA state
    return HalResult::OK;
}

void Esp32Ota::updateProgress(HalOtaState state, int percent, size_t downloaded, size_t total, const std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        currentProgress.state = state;
        currentProgress.percent = percent;
        currentProgress.bytesDownloaded = downloaded;
        currentProgress.totalBytes = total;
        currentProgress.errorMessage = error;
    }

    if (callback)
    {
        callback(currentProgress);
    }
}

namespace
{

class EspHttpSource : public OtaSource
{
public:
    EspHttpSource(const std::string& url, const std::map<std::string, std::string>& headers):
        url_(url), headers_(headers)
    {
    }

    ~EspHttpSource() override
    {
        if (client_)
        {
            esp_http_client_close(client_);
            esp_http_client_cleanup(client_);
        }
    }

    bool open(std::string& error) override
    {
        esp_http_client_config_t config = {};
        config.url = url_.c_str();
        // Per read; OtaStallWatchdog bounds the whole silence
        config.timeout_ms = 5000;
        config.buffer_size = 4096;
        config.buffer_size_tx = 1024;
        if (url_.find("https://") == 0)
            config.crt_bundle_attach = esp_crt_bundle_attach;

        client_ = esp_http_client_init(&config);
        if (!client_)
        {
            error = "HTTP client init failed";
            return false;
        }
        for (const auto& [key, value] : headers_)
            esp_http_client_set_header(client_, key.c_str(), value.c_str());

        esp_err_t err = esp_http_client_open(client_, 0);
        if (err != ESP_OK)
        {
            error = esp_err_to_name(err);
            return false;
        }
        contentLength_ = esp_http_client_fetch_headers(client_);
        return true;
    }

    int statusCode() override { return esp_http_client_get_status_code(client_); }

    int64_t contentLength() override { return contentLength_ > 0 ? contentLength_ : -1; }

    int read(char* buffer, int size) override
    {
        int n = esp_http_client_read(client_, buffer, size);
        if (n == -ESP_ERR_HTTP_EAGAIN)
            return kTimeout;
        return n;
    }

private:
    std::string url_;
    std::map<std::string, std::string> headers_;
    esp_http_client_handle_t client_ = nullptr;
    int64_t contentLength_ = -1;
};

class EspPartitionSink : public OtaSink
{
public:
    bool begin(int64_t imageSize, std::string& error) override
    {
        partition_ = esp_ota_get_next_update_partition(nullptr);
        if (!partition_)
        {
            error = "no OTA partition";
            return false;
        }
        ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%lx, size %lu)",
                 partition_->label, partition_->address, partition_->size);
        esp_err_t err = esp_ota_begin(partition_, imageSize > 0 ? imageSize : OTA_SIZE_UNKNOWN, &handle_);
        if (err != ESP_OK)
        {
            error = esp_err_to_name(err);
            return false;
        }
        open_ = true;
        return true;
    }

    bool write(const char* data, size_t size, std::string& error) override
    {
        esp_err_t err = esp_ota_write(handle_, data, size);
        if (err != ESP_OK)
        {
            error = esp_err_to_name(err);
            return false;
        }
        return true;
    }

    bool commit(std::string& error) override
    {
        // esp_ota_end releases the handle whatever its result
        open_ = false;
        esp_err_t err = esp_ota_end(handle_);
        if (err == ESP_OK)
            err = esp_ota_set_boot_partition(partition_);
        if (err != ESP_OK)
        {
            error = esp_err_to_name(err);
            return false;
        }
        return true;
    }

    void abort() override
    {
        if (open_)
            esp_ota_abort(handle_);
        open_ = false;
    }

private:
    const esp_partition_t* partition_ = nullptr;
    esp_ota_handle_t handle_ = 0;
    bool open_ = false;
};

}

void Esp32Ota::otaTask()
{
    ESP_LOGI(TAG, "OTA task started");

    const size_t bufferSize = 4096;
    char* buffer = static_cast<char*>(heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buffer)
    {
        updateInProgress = false;
        updateProgress(HalOtaState::Error, 0, 0, 0, "Memory allocation failed");
        return;
    }

    EspHttpSource source(updateUrl, authHeaders_);
    EspPartitionSink sink;

    OtaDownloadOptions options;
    options.expectedSha256 = expectedChecksum;
    options.nowMs = []() { return HAL::getInstance().getSystem().getTimeMs(); };
    options.abortRequested = &abortRequested;
    options.buffer = buffer;
    options.bufferSize = bufferSize;
    int lastPercent = -1;
    options.onProgress = [this, &lastPercent](size_t received, size_t total)
    {
        int percent = total > 0 ? static_cast<int>((received * 100) / total) : 0;
        if (percent == lastPercent)
            return;
        lastPercent = percent;
        updateProgress(HalOtaState::Downloading, percent, received, total);
    };

    options.onVerified = [this]() { updateProgress(HalOtaState::Installing, 100, 0, 0); };

    OtaDownloadResult result = runOtaDownload(source, sink, options);
    heap_caps_free(buffer);
    if (!result.ok)
    {
        ESP_LOGE(TAG, "OTA failed after %u bytes: %s", (unsigned)result.received, result.error.c_str());
        updateInProgress = false;
        updateProgress(HalOtaState::Error, 0, 0, 0, result.error);
        return;
    }

    ESP_LOGI(TAG, "OTA update successful (%u bytes), preparing to reboot", (unsigned)result.received);
    updateProgress(HalOtaState::Rebooting, 100, result.received, result.received);

    // Small delay to allow UI to update
    HAL::getInstance().getSystem().delay(2000);

    updateInProgress = false;

    // Turn off display before reboot to avoid flickering
    HAL::getInstance().getDisplay().backlightOff();
    HAL::getInstance().getDisplay().displayOff();
    HAL::getInstance().getSystem().delay(100);

    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

// Factory function implementation for ESP32
std::unique_ptr<HalOta> createOta()
{
    return std::make_unique<Esp32Ota>();
}

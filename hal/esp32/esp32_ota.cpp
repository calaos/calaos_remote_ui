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
#include "mbedtls/sha256.h"
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

void Esp32Ota::otaTask()
{
    ESP_LOGI(TAG, "OTA task started");

    // Use direct esp_ota API with esp_http_client for better control over HTTP/HTTPS
    esp_http_client_config_t httpConfig = {};
    httpConfig.url = updateUrl.c_str();
    httpConfig.timeout_ms = 30000;
    httpConfig.keep_alive_enable = true;
    httpConfig.buffer_size = 4096;
    httpConfig.buffer_size_tx = 1024;

    // For HTTPS URLs, use the certificate bundle
    bool isHttps = (updateUrl.find("https://") == 0);
    if (isHttps)
    {
        httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t httpClient = esp_http_client_init(&httpConfig);
    if (!httpClient)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        updateProgress(HalOtaState::Error, 0, 0, 0, "Failed to initialize HTTP client");
        updateInProgress = false;
        return;
    }

    // Set authentication headers
    for (const auto& [key, value] : authHeaders_)
    {
        esp_http_client_set_header(httpClient, key.c_str(), value.c_str());
        ESP_LOGD(TAG, "Set header: %s = %s", key.c_str(), value.c_str());
    }

    esp_err_t err = esp_http_client_open(httpClient, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        updateProgress(HalOtaState::Error, 0, 0, 0, std::string("HTTP connection failed: ") + esp_err_to_name(err));
        esp_http_client_cleanup(httpClient);
        updateInProgress = false;
        return;
    }

    int contentLength = esp_http_client_fetch_headers(httpClient);
    int statusCode = esp_http_client_get_status_code(httpClient);

    ESP_LOGI(TAG, "HTTP status: %d, content length: %d", statusCode, contentLength);

    if (statusCode != 200)
    {
        ESP_LOGE(TAG, "HTTP error: %d", statusCode);
        updateProgress(HalOtaState::Error, 0, 0, 0, "HTTP error: " + std::to_string(statusCode));
        esp_http_client_close(httpClient);
        esp_http_client_cleanup(httpClient);
        updateInProgress = false;
        return;
    }

    // Get the next OTA partition
    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition)
    {
        ESP_LOGE(TAG, "No OTA partition available");
        updateProgress(HalOtaState::Error, 0, 0, 0, "No OTA partition available");
        esp_http_client_close(httpClient);
        esp_http_client_cleanup(httpClient);
        updateInProgress = false;
        return;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%lx, size %lu)",
             updatePartition->label, updatePartition->address, updatePartition->size);

    esp_ota_handle_t otaHandle;
    err = esp_ota_begin(updatePartition, contentLength > 0 ? contentLength : OTA_SIZE_UNKNOWN, &otaHandle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        updateProgress(HalOtaState::Error, 0, 0, 0, std::string("OTA begin failed: ") + esp_err_to_name(err));
        esp_http_client_close(httpClient);
        esp_http_client_cleanup(httpClient);
        updateInProgress = false;
        return;
    }

    // Download buffer
    const int bufferSize = 4096;
    char* buffer = (char*)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        esp_ota_abort(otaHandle);
        esp_http_client_close(httpClient);
        esp_http_client_cleanup(httpClient);
        updateProgress(HalOtaState::Error, 0, 0, 0, "Memory allocation failed");
        updateInProgress = false;
        return;
    }

    // Initialize SHA256 context for download verification
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 = SHA256, 1 = SHA224

    int totalRead = 0;
    int lastPercent = -1;
    int imageSize = contentLength > 0 ? contentLength : 0;

    while (!abortRequested)
    {
        int readLen = esp_http_client_read(httpClient, buffer, bufferSize);
        if (readLen < 0)
        {
            ESP_LOGE(TAG, "HTTP read error");
            free(buffer);
            mbedtls_sha256_free(&sha256_ctx);
            esp_ota_abort(otaHandle);
            esp_http_client_close(httpClient);
            esp_http_client_cleanup(httpClient);
            updateProgress(HalOtaState::Error, 0, 0, 0, "HTTP read error");
            updateInProgress = false;
            return;
        }
        else if (readLen == 0)
        {
            // End of data
            break;
        }

        // Update SHA256 hash with downloaded data
        mbedtls_sha256_update(&sha256_ctx, (const unsigned char*)buffer, readLen);

        err = esp_ota_write(otaHandle, buffer, readLen);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buffer);
            mbedtls_sha256_free(&sha256_ctx);
            esp_ota_abort(otaHandle);
            esp_http_client_close(httpClient);
            esp_http_client_cleanup(httpClient);
            updateProgress(HalOtaState::Error, 0, 0, 0, std::string("OTA write failed: ") + esp_err_to_name(err));
            updateInProgress = false;
            return;
        }

        totalRead += readLen;

        // Update progress
        int percent = 0;
        if (imageSize > 0)
        {
            percent = (totalRead * 100) / imageSize;
        }

        if (percent != lastPercent)
        {
            lastPercent = percent;
            ESP_LOGI(TAG, "OTA progress: %d%% (%d/%d bytes)", percent, totalRead, imageSize);
            updateProgress(HalOtaState::Downloading, percent, totalRead, imageSize);
        }
    }

    free(buffer);
    esp_http_client_close(httpClient);
    esp_http_client_cleanup(httpClient);

    if (abortRequested)
    {
        ESP_LOGW(TAG, "OTA aborted by user");
        mbedtls_sha256_free(&sha256_ctx);
        esp_ota_abort(otaHandle);
        updateProgress(HalOtaState::Error, 0, 0, 0, "Update aborted");
        updateInProgress = false;
        return;
    }

    ESP_LOGI(TAG, "Download complete, total bytes: %d", totalRead);

    // Finalize SHA256 and verify checksum
    unsigned char sha256_hash[32];
    mbedtls_sha256_finish(&sha256_ctx, sha256_hash);
    mbedtls_sha256_free(&sha256_ctx);

    // Convert hash to hex string
    char calculatedChecksum[65];
    for (int i = 0; i < 32; i++)
    {
        sprintf(&calculatedChecksum[i * 2], "%02x", sha256_hash[i]);
    }
    calculatedChecksum[64] = '\0';

    ESP_LOGI(TAG, "Downloaded SHA256: %s", calculatedChecksum);
    ESP_LOGI(TAG, "Expected SHA256:   %s", expectedChecksum.c_str());

    // Verify checksum if provided
    if (!expectedChecksum.empty())
    {
        if (expectedChecksum != calculatedChecksum)
        {
            ESP_LOGE(TAG, "SHA256 MISMATCH! Download corrupted.");
            ESP_LOGE(TAG, "  Expected: %s", expectedChecksum.c_str());
            ESP_LOGE(TAG, "  Got:      %s", calculatedChecksum);
            esp_ota_abort(otaHandle);
            updateProgress(HalOtaState::Error, 0, 0, 0, "Download checksum mismatch - data corrupted during transfer");
            updateInProgress = false;
            return;
        }
        ESP_LOGI(TAG, "SHA256 checksum verified OK!");
    }
    else
    {
        ESP_LOGW(TAG, "No expected checksum provided, skipping verification");
    }

    updateProgress(HalOtaState::Installing, 100, totalRead, imageSize);

    // Finish OTA
    err = esp_ota_end(otaHandle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED)
        {
            ESP_LOGE(TAG, "OTA image validation failed");
            updateProgress(HalOtaState::Error, 0, 0, 0, "Image validation failed");
        }
        else
        {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            updateProgress(HalOtaState::Error, 0, 0, 0, std::string("OTA finish failed: ") + esp_err_to_name(err));
        }
        updateInProgress = false;
        return;
    }

    // Set the boot partition
    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        updateProgress(HalOtaState::Error, 0, 0, 0, std::string("Failed to set boot partition: ") + esp_err_to_name(err));
        updateInProgress = false;
        return;
    }

    ESP_LOGI(TAG, "OTA update successful, preparing to reboot");
    updateProgress(HalOtaState::Rebooting, 100, totalRead, imageSize);

    // Small delay to allow UI to update
    HAL::getInstance().getSystem().delay(2000);

    updateInProgress = false;

    // Reboot to apply update
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

// Factory function implementation for ESP32
std::unique_ptr<HalOta> createOta()
{
    return std::make_unique<Esp32Ota>();
}

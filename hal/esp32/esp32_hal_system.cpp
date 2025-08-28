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

static const char* TAG = "hal.system";
static const char* NVS_NAMESPACE = "calaos_config";

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
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);

    uint32_t flashSize = 0;
    esp_flash_get_size(esp_flash_default_chip, &flashSize);

    char info[256];
    snprintf(info, sizeof(info),
             "ESP32-P4 %dMB %s Flash, %d CPU cores, WiFi%s%s, Rev %d",
             (int)(flashSize / (1024 * 1024)),
             (chipInfo.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external",
             chipInfo.cores,
             (chipInfo.features & CHIP_FEATURE_WIFI_BGN) ? "/802.11bgn" : "",
             (chipInfo.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             chipInfo.revision);

    return std::string(info);
}

std::string Esp32HalSystem::getFirmwareVersion() const
{
    return std::string(esp_get_idf_version());
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
#include "esp32_hal.h"
#include "esp_log.h"

static const char* TAG = "ESP32_HAL";

Esp32HAL& Esp32HAL::getInstance()
{
    static Esp32HAL instance;
    return instance;
}

HalResult Esp32HAL::init()
{
    ESP_LOGI(TAG, "Initializing ESP32 HAL");

    system = std::make_unique<Esp32HalSystem>();
    if (system->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init system HAL");
        return HalResult::ERROR;
    }

    display = std::make_unique<Esp32HalDisplay>();
    if (display->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init display HAL");
        return HalResult::ERROR;
    }

    input = std::make_unique<Esp32HalInput>();
    if (input->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init input HAL");
        return HalResult::ERROR;
    }

    network = std::make_unique<Esp32HalNetwork>();
    if (network->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init network HAL");
        //return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "ESP32 HAL initialized successfully");
    return HalResult::OK;
}

HalResult Esp32HAL::deinit()
{
    if (network)
        network->deinit();
    if (input)
        input->deinit();
    if (display)
        display->deinit();
    if (system)
        system->deinit();

    network.reset();
    input.reset();
    display.reset();
    system.reset();

    ESP_LOGI(TAG, "ESP32 HAL deinitialized");
    return HalResult::OK;
}

HalDisplay& Esp32HAL::getDisplay()
{
    return *display;
}

HalInput& Esp32HAL::getInput()
{
    return *input;
}

HalNetwork& Esp32HAL::getNetwork()
{
    return *network;
}

HalSystem& Esp32HAL::getSystem()
{
    return *system;
}
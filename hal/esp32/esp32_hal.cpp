#include "esp32_hal.h"
#include "logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "flux.h"

static const char* TAG = "hal";

Esp32HAL& Esp32HAL::getInstance()
{
    static Esp32HAL instance;
    return instance;
}

HalResult Esp32HAL::init()
{
    ESP_LOGI(TAG, "Initializing ESP32 HAL (legacy mode)");

    // Legacy init - initialize everything sequentially
    if (initEssentials() != HalResult::OK)
        return HalResult::ERROR;

    // Initialize network synchronously in legacy mode
    network = std::make_unique<Esp32HalNetwork>();
    if (network->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init network HAL");
        //return HalResult::ERROR;
    }
    networkReady = true;

    ESP_LOGI(TAG, "ESP32 HAL initialized successfully");
    return HalResult::OK;
}

HalResult Esp32HAL::initEssentials()
{
    ESP_LOGI(TAG, "Initializing ESP32 HAL essentials (fast init)");

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

    ESP_LOGI(TAG, "ESP32 HAL essentials initialized successfully");
    return HalResult::OK;
}

HalResult Esp32HAL::initNetworkAsync()
{
    ESP_LOGI(TAG, "Starting network initialization task");

    // Create FreeRTOS task for network initialization
    BaseType_t ret = xTaskCreate(
        networkInitTask,
        "network_init",
        4096,  // Stack size
        this,  // Task parameter
        5,     // Priority
        nullptr // Task handle
    );

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create network init task");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Network initialization task started");
    return HalResult::OK;
}

void Esp32HAL::networkInitTask(void* parameter)
{
    Esp32HAL* hal = static_cast<Esp32HAL*>(parameter);
    ESP_LOGI(TAG, "Network init task started");

    // Initialize network in background
    hal->network = std::make_unique<Esp32HalNetwork>();
    if (hal->network->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init network HAL in async task");
    }
    else
    {
        ESP_LOGI(TAG, "Network HAL initialized successfully in async task");
        hal->networkReady = true;
    }

    // Task terminates automatically
    vTaskDelete(nullptr);
}

bool Esp32HAL::isNetworkReady() const
{
    return networkReady;
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
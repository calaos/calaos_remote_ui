#include "linux_hal.h"
#include "logging.h"
#include "lvgl.h"
#include <thread>
#include "../../flux/app_dispatcher.h"

static const char* TAG = "hal";

LinuxHAL& LinuxHAL::getInstance()
{
    static LinuxHAL instance;
    return instance;
}

HalResult LinuxHAL::init()
{
    ESP_LOGI(TAG, "Initializing Linux HAL (legacy mode)");
    
    // Legacy init - initialize everything sequentially
    if (initEssentials() != HalResult::OK)
        return HalResult::ERROR;
        
    // Initialize network synchronously in legacy mode
    network_ = std::make_unique<LinuxHalNetwork>();
    if (network_->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init network HAL");
        return HalResult::ERROR;
    }
    networkReady_ = true;

    ESP_LOGI(TAG, "Linux HAL initialized successfully");
    return HalResult::OK;
}

HalResult LinuxHAL::initEssentials()
{
    ESP_LOGI(TAG, "Initializing Linux HAL essentials (fast init)");

    // Initialize LVGL first
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    // Initialize system first
    system_ = std::make_unique<LinuxHalSystem>();
    if (system_->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init system HAL");
        return HalResult::ERROR;
    }

    // Initialize display
    display_ = std::make_unique<LinuxHalDisplay>();
    if (display_->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to init display HAL");
        return HalResult::ERROR;
    }

    // Initialize input (optional - some systems may not have separate input devices)
    input_ = std::make_unique<LinuxHalInput>();
    if (input_->init() != HalResult::OK)
    {
        ESP_LOGW(TAG, "No input device found, continuing without separate input HAL");
    }

    ESP_LOGI(TAG, "Linux HAL essentials initialized successfully");
    return HalResult::OK;
}

HalResult LinuxHAL::initNetworkAsync()
{
    ESP_LOGI(TAG, "Starting network initialization thread");
    
    // Create std::thread for network initialization on Linux
    std::thread networkThread([this]() {
        ESP_LOGI(TAG, "Network init thread started");
        
        // Initialize network in background
        network_ = std::make_unique<LinuxHalNetwork>();
        if (network_->init() != HalResult::OK)
        {
            ESP_LOGE(TAG, "Failed to init network HAL in async thread");
        }
        else
        {
            ESP_LOGI(TAG, "Network HAL initialized successfully in async thread");
            networkReady_ = true;
        }
    });
    
    // Detach thread to let it run independently
    networkThread.detach();
    
    ESP_LOGI(TAG, "Network initialization thread started");
    return HalResult::OK;
}

bool LinuxHAL::isNetworkReady() const
{
    return networkReady_;
}

HalResult LinuxHAL::deinit()
{
    if (network_) network_->deinit();
    if (input_) input_->deinit();
    if (display_) display_->deinit();
    if (system_) system_->deinit();

    network_.reset();
    input_.reset();
    display_.reset();
    system_.reset();

    // Deinitialize LVGL last
    lv_deinit();

    ESP_LOGI(TAG, "Linux HAL deinitialized");
    return HalResult::OK;
}

HalDisplay& LinuxHAL::getDisplay()
{
    return *display_;
}

HalInput& LinuxHAL::getInput()
{
    return *input_;
}

HalNetwork& LinuxHAL::getNetwork()
{
    return *network_;
}

HalSystem& LinuxHAL::getSystem()
{
    return *system_;
}
#include "app_main.h"
#include "logging.h"

#include "startup_page.h"
#include "smooth_ui_toolkit.h"
#include "../flux/flux.h"
#include "provisioning_manager.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#else
#include <iostream>
#endif

static const char* TAG = "main";

AppMain* g_appMain = nullptr;

AppMain::AppMain():
    hal(&HAL::getInstance()),
    initialized(false),
    running(false)
{
    g_appMain = this;
}

AppMain::~AppMain()
{
    g_appMain = nullptr;
    deinit();
}

bool AppMain::init()
{
    ESP_LOGI(TAG, "Using legacy full initialization");

    // Initialize Flux architecture
    initFlux();

    if (hal->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to initialize HAL");
        return false;
    }

    hal->getDisplay().backlightOn();
    hal->getDisplay().setBacklight(50);

#ifdef ESP_PLATFORM
    // Configure smooth_ui_toolkit HAL for ESP32
    smooth_ui_toolkit::ui_hal::on_get_tick([]() -> uint32_t {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000); // Convert microseconds to milliseconds
    });

    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    });

    ESP_LOGI(TAG, "Configured smooth_ui_toolkit HAL for ESP32");
#endif

    logSystemInfo();

    // Initialize provisioning manager
    if (!getProvisioningManager().init())
    {
        ESP_LOGE(TAG, "Failed to initialize provisioning manager");
        return false;
    }

    createBasicUi();

    initialized = true;
    running = true;
    ESP_LOGI(TAG, "Application initialized successfully");

    return true;
}

bool AppMain::initFast()
{
    ESP_LOGI(TAG, "Using fast initialization with async network");

    // Initialize Flux architecture
    initFlux();

    hal = &HAL::getInstance();

    // Fast init - only essentials (system, display, input)
    if (hal->initEssentials() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to initialize HAL essentials");
        return false;
    }

    hal->getDisplay().backlightOn();
    hal->getDisplay().setBacklight(50);

#ifdef ESP_PLATFORM
    // Configure smooth_ui_toolkit HAL for ESP32
    smooth_ui_toolkit::ui_hal::on_get_tick([]() -> uint32_t {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    });

    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    });

    ESP_LOGI(TAG, "Configured smooth_ui_toolkit HAL for ESP32");
#endif

    logSystemInfo();
    createBasicUi();  // UI is now visible!

    // Start network initialization in background
    if (hal->initNetworkAsync() != HalResult::OK)
    {
        ESP_LOGW(TAG, "Failed to start network initialization task");
    }

    initialized = true;
    running = true;
    ESP_LOGI(TAG, "Application initialized successfully (network initializing in background)");

    return true;
}

bool AppMain::isNetworkReady() const
{
    return hal ? hal->isNetworkReady() : false;
}

void AppMain::run()
{
    if (!initialized)
        return;

    while (running)
    {
        hal->getDisplay().lock(0);

        renderLoop();
        uint32_t timeMs = 5;

        #ifndef ESP_PLATFORM
        // On Linux, we need to handle LVGL timers ourselves
        timeMs = lv_timer_handler();

        // Check if display is still valid (window not closed) - Linux only
        lv_display_t* disp = hal->getDisplay().getLvglDisplay();
        if (!disp || !lv_display_get_driver_data(disp))
        {
            ESP_LOGI(TAG, "Display no longer valid, shutting down");
            hal->getDisplay().unlock();
            running = false;
            break;
        }
        #endif

        hal->getDisplay().unlock();

        // Ensure minimum delay to avoid watchdog issues
        if (timeMs < 1)
            timeMs = 1;

        hal->getSystem().delay(timeMs);
    }
}

void AppMain::stop()
{
    running = false;
}

void AppMain::deinit()
{
    running = false;
    if (hal && initialized)
    {
        hal->deinit();
        initialized = false;
    }
}

void AppMain::logSystemInfo()
{
    DisplayInfo displayInfo = hal->getDisplay().getDisplayInfo();
    std::string deviceInfo = hal->getSystem().getDeviceInfo();

    ESP_LOGI(TAG, "Display: %dx%d, %d-bit", displayInfo.width, displayInfo.height, displayInfo.colorDepth);
    ESP_LOGI(TAG, "Device: %s", deviceInfo.c_str());
}

void AppMain::createBasicUi()
{
    hal->getDisplay().lock(0);

    stackView = std::make_unique<StackView>(lv_screen_active());

    auto startupPage = std::make_unique<StartupPage>(lv_screen_active());
    stackView->push(std::move(startupPage));

    hal->getDisplay().unlock();
}

void AppMain::renderLoop()
{
    if (stackView)
        stackView->render();
}

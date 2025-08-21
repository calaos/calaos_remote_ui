#include "app_main.h"
#include "logging.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <iostream>
#endif

static const char* TAG = "main";

AppMain::AppMain() : hal(nullptr), initialized(false), running(false)
{
}

AppMain::~AppMain()
{
    deinit();
}

bool AppMain::init()
{
    hal = &HAL::getInstance();
    if (hal->init() != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to initialize HAL");
        return false;
    }

    hal->getDisplay().backlightOn();
    hal->getDisplay().setBacklight(50);

    logSystemInfo();
    createBasicUi();

    initialized = true;
    running = true;
    ESP_LOGI(TAG, "Application initialized successfully");

    return true;
}

void AppMain::run()
{
    if (!initialized)
        return;

    while (running)
    {
        uint32_t timeMs = 10;

        #ifndef ESP_PLATFORM
        timeMs = lv_timer_handler();
        
        // Check if display is still valid (window not closed)
        lv_display_t* disp = hal->getDisplay().getLvglDisplay();
        if (!disp || !lv_display_get_driver_data(disp))
        {
            ESP_LOGI(TAG, "Display no longer valid, shutting down");
            running = false;
            break;
        }
        #endif

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

    lv_obj_t* btn = lv_button_create(lv_screen_active());
    lv_obj_set_pos(btn, 10, 10);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_add_event_cb(btn, btnEventCb, LV_EVENT_ALL, NULL);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, "Button");
    lv_obj_center(label);

    hal->getDisplay().unlock();
}

void AppMain::btnEventCb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target_obj(e);
    if (code == LV_EVENT_CLICKED)
    {
        static uint8_t cnt = 0;
        cnt++;

        lv_obj_t* label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Button: %d", cnt);
    }
}
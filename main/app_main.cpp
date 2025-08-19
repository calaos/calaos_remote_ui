#include "app_main.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <iostream>
#endif

static const char* TAG = "APP_MAIN";

AppMain::AppMain() : hal(nullptr), initialized(false)
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
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to initialize HAL");
#else
        std::cerr << "Failed to initialize HAL" << std::endl;
#endif
        return false;
    }
    
    hal->getDisplay().backlightOn();
    hal->getDisplay().setBacklight(50);
    
    logSystemInfo();
    createBasicUi();
    
    initialized = true;
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Application initialized successfully");
#else
    std::cout << "Application initialized successfully" << std::endl;
#endif
    
    return true;
}

void AppMain::run()
{
    if (!initialized)
        return;
        
#ifdef ESP_PLATFORM
    while (1)
        vTaskDelay(pdMS_TO_TICKS(10));
#else
    while (1)
    {
        lv_timer_handler();
        hal->getSystem().delay(5);
    }
#endif
}

void AppMain::deinit()
{
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
    
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Display: %dx%d, %d-bit", displayInfo.width, displayInfo.height, displayInfo.colorDepth);
    ESP_LOGI(TAG, "Device: %s", deviceInfo.c_str());
#else
    std::cout << "Display: " << displayInfo.width << "x" << displayInfo.height 
              << ", " << displayInfo.colorDepth << "-bit" << std::endl;
    std::cout << "Device: " << deviceInfo << std::endl;
#endif
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
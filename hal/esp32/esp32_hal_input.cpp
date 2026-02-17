#include "esp32_hal_input.h"
#include "logging.h"
#include "bsp/touch.h"
#include "esp_lvgl_port.h"

static const char* TAG = "hal.input";

HalResult Esp32HalInput::init()
{
    esp_lcd_touch_handle_t tp = nullptr;
    if (bsp_touch_new(nullptr, &tp) != ESP_OK || !tp)
    {
        ESP_LOGE(TAG, "Failed to init touch controller");
        return HalResult::ERROR;
    }

    lv_display_t *disp = lv_display_get_default();
    if (!disp)
    {
        ESP_LOGE(TAG, "No default display found for touch");
        return HalResult::ERROR;
    }

    const lvgl_port_touch_cfg_t touchCfg = {
        .disp = disp,
        .handle = tp,
        .scale = { .x = 0, .y = 0 },
    };
    inputDevice = lvgl_port_add_touch(&touchCfg);
    if (!inputDevice)
    {
        ESP_LOGE(TAG, "Failed to add touch input to LVGL");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Input device initialized");
    return HalResult::OK;
}

HalResult Esp32HalInput::deinit()
{
    inputDevice = nullptr;
    return HalResult::OK;
}

lv_indev_t* Esp32HalInput::getLvglInputDevice()
{
    return inputDevice;
}
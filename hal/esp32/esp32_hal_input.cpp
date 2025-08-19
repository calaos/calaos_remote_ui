#include "esp32_hal_input.h"
#include "esp_log.h"
#include <src/lvgl_private.h>

static const char* TAG = "ESP32_HAL_INPUT";

HalResult Esp32HalInput::init()
{
    inputDevice = bsp_display_get_input_dev();
    if (!inputDevice)
    {
        ESP_LOGE(TAG, "Failed to get input device");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Input device initialized");
    return HalResult::OK;
}

HalResult Esp32HalInput::deinit()
{
    inputDevice = nullptr;
    touchCallback = nullptr;
    return HalResult::OK;
}

HalResult Esp32HalInput::registerTouchCallback(TouchEventCallback callback)
{
    touchCallback = callback;
    return HalResult::OK;
}

HalResult Esp32HalInput::readTouch(TouchData& touchData)
{
    if (!inputDevice)
        return HalResult::ERROR;

    lv_point_t data;
    lv_indev_read(inputDevice);
    lv_indev_get_point(inputDevice, &data);

    touchData.x = data.x;
    touchData.y = data.y;
    touchData.pressed = (inputDevice->state == LV_INDEV_STATE_PRESSED);

    if (touchCallback && touchData.pressed)
        touchCallback(touchData);

    return HalResult::OK;
}

lv_indev_t* Esp32HalInput::getLvglInputDevice()
{
    return inputDevice;
}
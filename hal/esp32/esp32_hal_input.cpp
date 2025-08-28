#include "esp32_hal_input.h"
#include "logging.h"

static const char* TAG = "hal.input";

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
    return HalResult::OK;
}

lv_indev_t* Esp32HalInput::getLvglInputDevice()
{
    return inputDevice;
}
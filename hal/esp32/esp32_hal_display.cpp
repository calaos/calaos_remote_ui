#include "esp32_hal_display.h"
#include "logging.h"
#include "bsp_board_extra.h"
#include "bsp/display.h"

static const char* TAG = "hal.display";

HalResult Esp32HalDisplay::init()
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 100, // Increase buffer to 100 lines instead of 50
        .double_buffer = true, // Enable double buffering to prevent tearing
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };

    display = bsp_display_start_with_config(&cfg);
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to get LVGL display");
        return HalResult::ERROR;
    }

    lv_display_set_dpi(display, 180);

    // lv_display_set_rotation(display, LV_DISP_ROTATION_90);

    displayInfo.width = 720;
    displayInfo.height = 720;
    displayInfo.colorDepth = lv_display_get_dpi(display);

    ESP_LOGI(TAG, "Display initialized: %dx%d, %d-bit",
             displayInfo.width, displayInfo.height, displayInfo.colorDepth);

    return HalResult::OK;
}

HalResult Esp32HalDisplay::deinit()
{
    display = nullptr;
    return HalResult::OK;
}

DisplayInfo Esp32HalDisplay::getDisplayInfo() const
{
    return displayInfo;
}

HalResult Esp32HalDisplay::setBacklight(uint8_t brightness)
{
    esp_err_t ret = bsp_display_brightness_set(brightness);
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalDisplay::backlightOn()
{
    esp_err_t ret = bsp_display_backlight_on();
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalDisplay::backlightOff()
{
    esp_err_t ret = bsp_display_backlight_off();
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

void Esp32HalDisplay::lock(uint32_t timeoutMs)
{
    bsp_display_lock(timeoutMs);
}

void Esp32HalDisplay::unlock()
{
    bsp_display_unlock();
}

lv_display_t* Esp32HalDisplay::getLvglDisplay()
{
    return display;
}
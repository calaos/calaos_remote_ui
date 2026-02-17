#include "esp32_hal_display.h"
#include "logging.h"
#include "board_config.h"
#include "bsp_board_extra.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lvgl_port.h"

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

    // Initialize LVGL port
    if (lvgl_port_init(&cfg.lvgl_port_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init LVGL port");
        return HalResult::ERROR;
    }

    if (bsp_display_brightness_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init display brightness");
        return HalResult::ERROR;
    }

    // Create display and get panel handle
    bsp_lcd_handles_t lcdHandles = {};
    if (bsp_display_new_with_handles(nullptr, &lcdHandles) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create display panel");
        return HalResult::ERROR;
    }
    panelHandle = lcdHandles.panel;

    // Configure LVGL display
    const lvgl_port_display_cfg_t dispCfg = {
        .io_handle = lcdHandles.io,
        .panel_handle = lcdHandles.panel,
        .control_handle = lcdHandles.control,
        .buffer_size = cfg.buffer_size,
        .double_buffer = cfg.double_buffer,
        .trans_size = 0,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = cfg.flags.buff_dma,
            .buff_spiram = cfg.flags.buff_spiram,
            .sw_rotate = cfg.flags.sw_rotate,
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
            .full_refresh = 0,
            .direct_mode = 0,
        }
    };

    const lvgl_port_display_dsi_cfg_t dsiCfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };

    display = lvgl_port_add_disp_dsi(&dispCfg, &dsiCfg);
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return HalResult::ERROR;
    }

    if (!display)
    {
        ESP_LOGE(TAG, "Failed to get LVGL display");
        return HalResult::ERROR;
    }

    lv_display_set_dpi(display, 180);

    // lv_display_set_rotation(display, LV_DISP_ROTATION_90);

    displayInfo.width = BOARD_DISPLAY_WIDTH;
    displayInfo.height = BOARD_DISPLAY_HEIGHT;
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

HalResult Esp32HalDisplay::displayOff()
{
    if (!panelHandle)
        return HalResult::ERROR;

    esp_err_t ret = esp_lcd_panel_disp_on_off(panelHandle, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to turn off display panel: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }
    return HalResult::OK;
}

void Esp32HalDisplay::lock(uint32_t timeoutMs)
{
    bsp_display_lock(timeoutMs);
}

bool Esp32HalDisplay::tryLock(uint32_t timeoutMs)
{
    return bsp_display_lock(timeoutMs);
}

void Esp32HalDisplay::unlock()
{
    bsp_display_unlock();
}

lv_display_t* Esp32HalDisplay::getLvglDisplay()
{
    return display;
}
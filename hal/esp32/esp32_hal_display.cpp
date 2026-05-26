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
    // Panel native (pre-rotation) dimensions, derived from BOARD_DISPLAY_* + rotation.
    // We don't read BSP_LCD_H_RES/V_RES: two BSPs export bsp/display.h and the 4b
    // (managed) one always defines 720x720, which wins the include search even when
    // EXCLUDE_COMPONENTS removes its sources.
    #if BOARD_DISPLAY_ROTATION == 90 || BOARD_DISPLAY_ROTATION == 270
        constexpr int kPanelHRes = BOARD_DISPLAY_HEIGHT;
        constexpr int kPanelVRes = BOARD_DISPLAY_WIDTH;
    #else
        constexpr int kPanelHRes = BOARD_DISPLAY_WIDTH;
        constexpr int kPanelVRes = BOARD_DISPLAY_HEIGHT;
    #endif

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = kPanelHRes * 200, // 200 lines: large partial buffer to amortize PPA rotation overhead
        .double_buffer = true, // Enable double buffering to prevent tearing
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };
    // Default LVGL task stack (7168 B) overflows on LVGL 9.5 + PPA when
    // building deep widget trees (CalaosPage + tabs + AboutPage). Bump to 16 KB.
    cfg.lvgl_port_cfg.task_stack = 16384;

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
        .hres = kPanelHRes,
        .vres = kPanelVRes,
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

#if BOARD_DISPLAY_ROTATION != 0
    if (lvgl_port_lock(0))
    {
    #if BOARD_DISPLAY_ROTATION == 90
        lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    #elif BOARD_DISPLAY_ROTATION == 180
        lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);
    #elif BOARD_DISPLAY_ROTATION == 270
        lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    #endif
        lvgl_port_unlock();
    }
#endif

    displayInfo.width = BOARD_DISPLAY_WIDTH;
    displayInfo.height = BOARD_DISPLAY_HEIGHT;
    displayInfo.colorDepth = BOARD_DISPLAY_COLOR_DEPTH;

    ESP_LOGI(TAG, "Display initialized: %dx%d, %d-bit (rot=%d, lv hres=%d vres=%d, scr=%dx%d)",
             displayInfo.width, displayInfo.height, displayInfo.colorDepth,
             (int)lv_display_get_rotation(display),
             (int)lv_display_get_horizontal_resolution(display),
             (int)lv_display_get_vertical_resolution(display),
             (int)lv_obj_get_width(lv_screen_active()),
             (int)lv_obj_get_height(lv_screen_active()));

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
    return bsp_display_lock(timeoutMs) == ESP_OK;
}

void Esp32HalDisplay::unlock()
{
    bsp_display_unlock();
}

lv_display_t* Esp32HalDisplay::getLvglDisplay()
{
    return display;
}
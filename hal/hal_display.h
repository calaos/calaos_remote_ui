#pragma once

#include "hal_types.h"
#include "lvgl.h"
#include "board_config.h"

class HalDisplay
{
public:
    virtual ~HalDisplay() = default;

    virtual HalResult init() = 0;
    virtual HalResult deinit() = 0;
    virtual DisplayInfo getDisplayInfo() const = 0;
    virtual HalResult setBacklight(uint8_t brightness) = 0;
    virtual HalResult backlightOn() = 0;
    virtual HalResult backlightOff() = 0;
    virtual HalResult displayOff() { return HalResult::OK; }
    virtual void lock(uint32_t timeoutMs = 0) = 0;
    virtual bool tryLock(uint32_t timeoutMs) = 0;
    virtual void unlock() = 0;

    virtual lv_display_t* getLvglDisplay() = 0;

    int getWidth() const { return getDisplayInfo().width; }
    int getHeight() const { return getDisplayInfo().height; }
    int getColorDepth() const { return getDisplayInfo().colorDepth; }
    int getPreferedGridWidth() const { return BOARD_PREFERED_GRID_WIDTH; }
    int getPreferedGridHeight() const { return BOARD_PREFERED_GRID_HEIGHT; }
};
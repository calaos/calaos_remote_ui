#pragma once

#include "../hal_display.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

class Esp32HalDisplay : public HalDisplay
{
public:
    HalResult init() override;
    HalResult deinit() override;
    DisplayInfo getDisplayInfo() const override;
    HalResult setBacklight(uint8_t brightness) override;
    HalResult backlightOn() override;
    HalResult backlightOff() override;
    void lock(uint32_t timeoutMs = 0) override;
    void unlock() override;
    lv_display_t* getLvglDisplay() override;

private:
    lv_display_t* display = nullptr;
    DisplayInfo displayInfo;
};
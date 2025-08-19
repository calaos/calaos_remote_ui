#pragma once

#include "../hal_input.h"
#include "bsp/esp-bsp.h"

class Esp32HalInput : public HalInput
{
public:
    HalResult init() override;
    HalResult deinit() override;
    HalResult registerTouchCallback(TouchEventCallback callback) override;
    HalResult readTouch(TouchData& touchData) override;
    lv_indev_t* getLvglInputDevice() override;

private:
    lv_indev_t* inputDevice = nullptr;
    TouchEventCallback touchCallback;
};
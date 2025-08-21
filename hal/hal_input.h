#pragma once

#include "hal_types.h"
#include "lvgl.h"

class HalInput
{
public:
    virtual ~HalInput() = default;
    
    virtual HalResult init() = 0;
    virtual HalResult deinit() = 0;
    
    virtual lv_indev_t* getLvglInputDevice() = 0;
};
#pragma once

#include "lvgl.h"
#include "hal.h"

class AppMain
{
public:
    AppMain();
    ~AppMain();
    
    bool init();
    void run();
    void deinit();

private:
    void createBasicUi();
    void logSystemInfo();
    static void btnEventCb(lv_event_t* e);
    
    HAL* hal;
    bool initialized;
};
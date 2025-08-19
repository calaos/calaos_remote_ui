#pragma once

#include "../hal_input.h"
#include <thread>
#include <atomic>

class LinuxHalInput : public HalInput
{
public:
    HalResult init() override;
    HalResult deinit() override;
    HalResult registerTouchCallback(TouchEventCallback callback) override;
    HalResult readTouch(TouchData& touchData) override;
    lv_indev_t* getLvglInputDevice() override;

private:
    void touchThreadFunction();
    static void lvglInputRead(lv_indev_t* indev, lv_indev_data_t* data);

    lv_indev_t* inputDevice = nullptr;
    TouchEventCallback touchCallback;
    std::thread touchThread;
    std::atomic<bool> threadRunning;
    TouchData lastTouchData;
    int touchFd = -1;
};
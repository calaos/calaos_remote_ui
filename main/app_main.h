#pragma once

#include "lvgl.h"
#include "hal.h"
#include "stack_view.h"
#include <memory>

class AppMain
{
public:
    AppMain();
    ~AppMain();

    bool init();
    bool initFast();
    void run();
    void deinit();
    void stop();

    StackView* getStackView() { return stackView.get(); }
    bool isNetworkReady() const;

private:
    void createBasicUi();
    void logSystemInfo();
    void renderLoop();

    HAL* hal;
    bool initialized;
    bool running;
    std::unique_ptr<StackView> stackView;
};
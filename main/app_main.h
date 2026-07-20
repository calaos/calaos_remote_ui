#pragma once

#include "lvgl.h"
#include "hal.h"
#include "stack_view.h"
#include <memory>

class OtaUpdateScreen;
class ScreenSaver;
class NotificationToast;

class AppMain
{
public:
    AppMain();
    ~AppMain();

    bool init();
    bool initFast();
    bool initBenchmark(); // PERF-01: boot into the perf_bench harness (PERF_BENCH builds)
    void run();
    void deinit();
    void stop();

    StackView* getStackView() { return stackView.get(); }
    bool isNetworkReady() const;

private:
    void createBasicUi();
    void initOtaManager();
    void logSystemInfo();
    void renderLoop();
    HAL* hal;
    bool initialized;
    bool running;
    std::unique_ptr<StackView> stackView;
    std::unique_ptr<OtaUpdateScreen> otaScreen;
    std::unique_ptr<ScreenSaver> screenSaver;
    std::unique_ptr<NotificationToast> notificationToast;
};
#pragma once

#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "../flux/flux.h"
#include "lvgl_timer.h"
#include "calaos_protocol.h"
#include <memory>
#include <string>

class ScreenSaver : public smooth_ui_toolkit::lvgl_cpp::Container
{
public:
    ScreenSaver(lv_obj_t* parent);
    ~ScreenSaver();

    void update();

private:
    void createUI();
    void activate();
    void deactivate();
    void updateClock();
    void applyConfig(const CalaosProtocol::RemoteUIConfig& config);
    void rebuildClockUI();

    std::string formatDate(const struct tm& tm);

    static const lv_font_t* selectFont(int availableHeight, int availableWidth,
                                       const char* referenceText);

    static void onClicked(lv_event_t* e);

    // UI elements
    lv_obj_t* timeLabel = nullptr;
    lv_obj_t* dateLabel = nullptr;

    // Clock update timer (1s, active only when screensaver visible + clock mode)
    std::unique_ptr<LvglTimer> clockTimer;

    // State
    bool active_ = false;
    SubscriptionId subscriptionId_ = 0;

    // Cached config
    int screensaverTimeout_ = 0;
    int screensaverDimming_ = 0;
    int normalBrightness_ = 80;
    std::string screensaverMode_ = "black";
    std::string clockTimezone_;
    bool clockUse24h_ = true;
    bool clockShowDate_ = false;
    bool clockShowSeconds_ = false;
    std::string clockDateFormat_ = "DD/MM/YYYY";
};

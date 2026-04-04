#pragma once

#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "../flux/flux.h"
#include <string>

class NotificationToast : public smooth_ui_toolkit::lvgl_cpp::Container
{
public:
    NotificationToast(lv_obj_t* parent);
    ~NotificationToast();

    void show(const std::string& message);
    void hide();

private:
    void createUI();
    void onStateChanged(const AppState& state);
    static void timerCallback(lv_timer_t* timer);
    static void clickCallback(lv_event_t* e);

    lv_obj_t* messageLabel_ = nullptr;
    lv_timer_t* hideTimer_ = nullptr;
    SubscriptionId subscriptionId_ = 0;
    uint32_t lastNotificationVersion_ = 0;
};

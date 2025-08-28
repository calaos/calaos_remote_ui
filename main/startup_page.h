#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"

class StartupPage: public PageBase
{
public:
    StartupPage(lv_obj_t *parent);
    void render() override;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> logo;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> networkStatusLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Spinner> networkSpinner;
    smooth_ui_toolkit::Animate logoDropAnimation;
    smooth_ui_toolkit::Animate networkStatusAnimation;

    void initLogoAnimation();
    void updateNetworkStatus();
    static void testButtonCb(lv_event_t* e);

    bool lastNetworkState;
};

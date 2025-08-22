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
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> testButton;
    smooth_ui_toolkit::Animate logoDropAnimation;
    void initLogoAnimation();
    static void testButtonCb(lv_event_t* e);
};

#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"

class TestPage: public PageBase
{
public:
    TestPage(lv_obj_t *parent, const char* title = "Test Page");
    void render() override;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> titleLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> backButton;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> nextButton;
    
    static void backButtonCb(lv_event_t* e);
    static void nextButtonCb(lv_event_t* e);
};
#pragma once

#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"

class PageBase : public smooth_ui_toolkit::lvgl_cpp::Container
{
public:
    PageBase(lv_obj_t *parent);
    virtual ~PageBase() = default;

    virtual void render() = 0;

protected:
    void setupFullScreen();
};
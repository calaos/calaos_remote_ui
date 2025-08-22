#include "page_base.h"

using namespace smooth_ui_toolkit;

PageBase::PageBase(lv_obj_t *parent):
    lvgl_cpp::Container(parent)
{
    setupFullScreen();
}

void PageBase::setupFullScreen()
{
    lv_obj_remove_style_all(get());
    setWidth(LV_PCT(100));
    setHeight(LV_PCT(100));
    setPos(0, 0);
}
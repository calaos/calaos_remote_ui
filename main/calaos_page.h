#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "flux.h"
#include <memory>

class CalaosPage: public PageBase
{
public:
    CalaosPage(lv_obj_t *parent);
    ~CalaosPage();
    void render() override;

private:
    // Tab view
    lv_obj_t* tabview;
    lv_obj_t* tabContent[3];

    // Page indicator
    lv_obj_t* tabLabels[3];
    lv_obj_t* pageIndicatorContainer;
    lv_obj_t* pageIndicatorDots[3];

    // State management
    CalaosWebSocketState lastWebSocketState;

    void createTabView();
    void createPageIndicator();
    void createSubPages();
    void updatePageIndicator(uint32_t activeTab);
    void onStateChanged(const AppState& state);
    void onTabChanged(uint32_t activeTab);

    static void tabViewEventCb(lv_event_t* e);
};

#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "flux.h"
#include "calaos_widget.h"
#include <memory>
#include <vector>
#include <string>

class CalaosPage: public PageBase
{
public:
    CalaosPage(lv_obj_t *parent);
    ~CalaosPage();
    void render() override;

private:
    // Tab view (CHANGED: dynamic instead of fixed array)
    lv_obj_t* tabview;
    std::vector<lv_obj_t*> tabContent;        // Dynamic tabs

    // Page indicator (CHANGED: dynamic, may be nullptr if 1 page)
    lv_obj_t* pageIndicatorContainer;
    std::vector<lv_obj_t*> pageIndicatorDots; // Dynamic dots

    // NEW: Widget storage per page
    std::vector<std::vector<std::unique_ptr<CalaosWidget>>> pageWidgets;

    // State management
    CalaosWebSocketState lastWebSocketState;
    std::string lastConfigJson;  // NEW: Detect config changes
    SubscriptionId subscriptionId_;  // NEW: Track AppStore subscription

    void createTabView();
    void createPageIndicator(int numPages);  // CHANGED: parameter numPages
    void updatePageIndicator(uint32_t activeTab);

    // NEW: Dynamic page/widget management
    void destroyPages();
    void createPagesFromConfig(const CalaosProtocol::PagesConfig& config);
    void createWidgetsForPage(int pageIndex,
                             const CalaosProtocol::PageConfig& pageConfig,
                             const GridLayoutInfo& gridInfo);

    void onStateChanged(const AppState& state);
    void onTabChanged(uint32_t activeTab);

    static void tabViewEventCb(lv_event_t* e);
};

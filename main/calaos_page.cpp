#include "calaos_page.h"
#include "theme.h"
#include "app_main.h"
#include "hal.h"
#include "logging.h"

static const char* TAG = "CalaosPage";
extern AppMain* g_appMain;

CalaosPage::CalaosPage(lv_obj_t *parent):
    PageBase(parent),
    tabview(nullptr),
    pageIndicatorContainer(nullptr)
{
    ESP_LOGI(TAG, "Creating CalaosPage");

    // Initialize arrays
    for (int i = 0; i < 3; i++)
    {
        tabContent[i] = nullptr;
        tabLabels[i] = nullptr;
        pageIndicatorDots[i] = nullptr;
    }

    // Set background color
    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    // Create components
    createTabView();
    createSubPages();
    createPageIndicator();

    // Subscribe to state changes
    AppStore::getInstance().subscribe([this](const AppState& state)
    {
        onStateChanged(state);
    });

    lastWebSocketState = AppStore::getInstance().getState().websocket;
}

CalaosPage::~CalaosPage()
{
    ESP_LOGI(TAG, "Destroying CalaosPage");
}

void CalaosPage::render()
{
    // Currently no animations, but this is where they would be updated
}

void CalaosPage::createTabView()
{
    // Create tabview using LVGL C API
    tabview = lv_tabview_create(get());

    // Position tabview to fill parent
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(tabview, 0, 0);

    // Hide tab bar - we'll use custom page indicator instead
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 0);

    // Set up event callback for tab changes
    lv_obj_add_event_cb(tabview, tabViewEventCb, LV_EVENT_VALUE_CHANGED, this);

    ESP_LOGI(TAG, "Tab view created");
}

void CalaosPage::createSubPages()
{
    // Create 3 tabs
    tabContent[0] = lv_tabview_add_tab(tabview, "Page 1");
    tabContent[1] = lv_tabview_add_tab(tabview, "Page 2");
    tabContent[2] = lv_tabview_add_tab(tabview, "Page 3");

    // Add center label to each tab
    for (int i = 0; i < 3; i++)
    {
        tabLabels[i] = lv_label_create(tabContent[i]);
        char labelText[20];
        snprintf(labelText, sizeof(labelText), "Calaos Page #%d", i + 1);
        lv_label_set_text(tabLabels[i], labelText);
        lv_obj_align(tabLabels[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(tabLabels[i], &lv_font_montserrat_48, LV_PART_MAIN);
        lv_obj_set_style_text_color(tabLabels[i], theme_color_white, LV_PART_MAIN);

        // Set tab background to theme_color_black
        lv_obj_set_style_bg_color(tabContent[i], theme_color_black, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tabContent[i], LV_OPA_COVER, LV_PART_MAIN);
    }

    ESP_LOGI(TAG, "Sub-pages created with labels in each tab");
}

void CalaosPage::createPageIndicator()
{
    // Create container for indicator dots
    pageIndicatorContainer = lv_obj_create(get());
    lv_obj_set_size(pageIndicatorContainer, 100, 20);
    lv_obj_align(pageIndicatorContainer, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(pageIndicatorContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(pageIndicatorContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pageIndicatorContainer, 0, LV_PART_MAIN);

    // Create 3 dots
    lv_color_t inactiveColor = lv_color_make(0x66, 0x66, 0x66);

    for (int i = 0; i < 3; i++)
    {
        pageIndicatorDots[i] = lv_obj_create(pageIndicatorContainer);
        lv_obj_set_size(pageIndicatorDots[i], 12, 12);
        lv_obj_set_pos(pageIndicatorDots[i], i * 30 + 10, 4);
        lv_obj_set_style_radius(pageIndicatorDots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(pageIndicatorDots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pageIndicatorDots[i], inactiveColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pageIndicatorDots[i], LV_OPA_COVER, LV_PART_MAIN);
    }

    // Set first dot to active
    updatePageIndicator(0);

    ESP_LOGI(TAG, "Page indicator created");
}

void CalaosPage::updatePageIndicator(uint32_t activeTab)
{
    lv_color_t inactiveColor = lv_color_make(0x66, 0x66, 0x66);

    for (int i = 0; i < 3; i++)
    {
        if (i == activeTab)
        {
            lv_obj_set_style_bg_color(pageIndicatorDots[i], theme_color_blue, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(pageIndicatorDots[i], inactiveColor, LV_PART_MAIN);
        }
    }
}

void CalaosPage::tabViewEventCb(lv_event_t* e)
{
    CalaosPage* page = static_cast<CalaosPage*>(lv_event_get_user_data(e));
    if (!page)
        return;

    lv_obj_t* tabview = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint32_t activeTab = lv_tabview_get_tab_active(tabview);
    page->onTabChanged(activeTab);
}

void CalaosPage::onTabChanged(uint32_t activeTab)
{
    ESP_LOGI(TAG, "Tab changed to: %d", activeTab);
    updatePageIndicator(activeTab);
}

void CalaosPage::onStateChanged(const AppState& state)
{
    // Check for disconnection
    if (!state.websocket.isConnected && lastWebSocketState.isConnected)
    {
        ESP_LOGI(TAG, "WebSocket disconnected - returning to StartupPage");

        // Pop this page from StackView
        if (g_appMain && g_appMain->getStackView())
        {
            // Use display lock for thread safety
            if (HAL::getInstance().getDisplay().tryLock(100))
            {
                g_appMain->getStackView()->pop(stack_animation_type::SlideVertical);
                HAL::getInstance().getDisplay().unlock();
            }
        }
    }

    lastWebSocketState = state.websocket;

    // Future: Handle state.ioStates updates for widget rendering
    // Future: Handle state.config updates for page reconfiguration
}

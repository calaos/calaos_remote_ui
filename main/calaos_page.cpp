#include "calaos_page.h"
#include "theme.h"
#include "app_main.h"
#include "hal.h"
#include "logging.h"
#include "widget_factory.h"

static const char* TAG = "CalaosPage";
extern AppMain* g_appMain;

CalaosPage::CalaosPage(lv_obj_t *parent):
    PageBase(parent),
    tabview(nullptr),
    pageIndicatorContainer(nullptr)
{
    ESP_LOGI(TAG, "Creating CalaosPage");

    // Set background color
    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    // Create tabview (pages will be added when config is received)
    createTabView();

    // Subscribe to state changes
    subscriptionId_ = AppStore::getInstance().subscribe([this](const AppState& state)
    {
        onStateChanged(state);
    });

    // Get initial state
    const AppState& initialState = AppStore::getInstance().getState();
    lastWebSocketState = initialState.websocket;
    lastConfigJson = "";

    // Try to create pages from current config if available
    if (!initialState.config.pages_json.empty())
    {
        ESP_LOGI(TAG, "Initial config available, creating pages");
        lastConfigJson = initialState.config.pages_json;
        try
        {
            auto pagesConfig = initialState.config.getParsedPages();
            createPagesFromConfig(pagesConfig);
        }
        catch (const std::exception& e)
        {
            ESP_LOGE(TAG, "Failed to parse initial config: %s", e.what());
        }
    }
    else
    {
        ESP_LOGI(TAG, "No initial config, waiting for remote_ui_config_update");
    }
}

CalaosPage::~CalaosPage()
{
    ESP_LOGI(TAG, "Destroying CalaosPage");

    // Unsubscribe from AppStore to prevent dangling pointer
    AppStore::getInstance().unsubscribe(subscriptionId_);
}

void CalaosPage::render()
{
    // Update animations for all widgets on current tab
    if (tabview)
    {
        uint32_t currentTab = lv_tabview_get_tab_active(tabview);
        if (currentTab < pageWidgets.size())
        {
            for (auto& widget : pageWidgets[currentTab])
            {
                if (widget)
                    widget->render();
            }
        }
    }
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


void CalaosPage::createPageIndicator(int numPages)
{
    // Don't create indicator if only 1 page
    if (numPages <= 1)
    {
        ESP_LOGI(TAG, "Only %d page(s), not creating page indicator", numPages);
        pageIndicatorContainer = nullptr;
        return;
    }

    // Create container for indicator dots
    int containerWidth = numPages * 30 + 20;  // Dynamic width based on number of pages
    pageIndicatorContainer = lv_obj_create(get());
    lv_obj_set_size(pageIndicatorContainer, containerWidth, 20);
    lv_obj_align(pageIndicatorContainer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(pageIndicatorContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(pageIndicatorContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pageIndicatorContainer, 0, LV_PART_MAIN);

    // Create dots dynamically
    lv_color_t inactiveColor = lv_color_make(0x66, 0x66, 0x66);

    pageIndicatorDots.clear();
    for (int i = 0; i < numPages; i++)
    {
        lv_obj_t* dot = lv_obj_create(pageIndicatorContainer);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_pos(dot, i * 30 + 10, 4);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, inactiveColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);

        pageIndicatorDots.push_back(dot);
    }

    // Set first dot to active
    updatePageIndicator(0);

    ESP_LOGI(TAG, "Page indicator created with %d dots", numPages);
}

void CalaosPage::updatePageIndicator(uint32_t activeTab)
{
    if (pageIndicatorDots.empty())
        return;

    lv_color_t inactiveColor = lv_color_make(0x66, 0x66, 0x66);

    for (size_t i = 0; i < pageIndicatorDots.size(); i++)
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

void CalaosPage::destroyPages()
{
    ESP_LOGI(TAG, "Destroying existing pages");

    // Clear widgets (unique_ptr auto-deletes)
    pageWidgets.clear();

    // Delete page indicator
    if (pageIndicatorContainer)
    {
        lv_obj_del(pageIndicatorContainer);
        pageIndicatorContainer = nullptr;
    }
    pageIndicatorDots.clear();

    // Delete tabs (LVGL objects)
    for (auto tab : tabContent)
    {
        if (tab)
            lv_obj_del(tab);
    }
    tabContent.clear();

    ESP_LOGI(TAG, "Pages destroyed");
}

void CalaosPage::createPagesFromConfig(const CalaosProtocol::PagesConfig& config)
{
    int numPages = config.pages.size();

    ESP_LOGI(TAG, "Creating %d page(s) from config (grid: %dx%d)",
            numPages, config.grid_width, config.grid_height);

    if (numPages == 0)
    {
        ESP_LOGW(TAG, "No pages in config - creating empty placeholder");
        // Create one empty tab
        lv_obj_t* tab = lv_tabview_add_tab(tabview, "Empty");
        lv_obj_set_style_bg_color(tab, theme_color_black, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);

        // Show "No pages configured" message
        lv_obj_t* label = lv_label_create(tab);
        lv_label_set_text(label, "No pages configured");
        lv_obj_set_style_text_color(label, theme_color_white, 0);
        lv_obj_set_style_text_font(label, &roboto_medium_24, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

        tabContent.push_back(tab);
        return;
    }

    // Calculate grid layout info
    GridLayoutInfo gridInfo;
    gridInfo.gridWidth = config.grid_width;
    gridInfo.gridHeight = config.grid_height;
    gridInfo.screenWidth = 720;
    gridInfo.screenHeight = 720;
    gridInfo.cellWidth = gridInfo.screenWidth / gridInfo.gridWidth;
    gridInfo.cellHeight = gridInfo.screenHeight / gridInfo.gridHeight;
    gridInfo.padding = 8;

    ESP_LOGI(TAG, "Grid: %dx%d, Cell size: %dx%d pixels",
            gridInfo.gridWidth, gridInfo.gridHeight,
            gridInfo.cellWidth, gridInfo.cellHeight);

    // Create tabs
    for (int i = 0; i < numPages; i++)
    {
        char tabName[32];
        snprintf(tabName, sizeof(tabName), "Page %d", i + 1);

        lv_obj_t* tab = lv_tabview_add_tab(tabview, tabName);
        tabContent.push_back(tab);

        // Style tab
        lv_obj_set_style_bg_color(tab, theme_color_black, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

        // Create widgets for this page
        createWidgetsForPage(i, config.pages[i], gridInfo);
    }

    // Create page indicator (only if > 1 page)
    createPageIndicator(numPages);

    ESP_LOGI(TAG, "Created %d page(s) successfully", numPages);
}

void CalaosPage::createWidgetsForPage(int pageIndex,
                                     const CalaosProtocol::PageConfig& pageConfig,
                                     const GridLayoutInfo& gridInfo)
{
    std::vector<std::unique_ptr<CalaosWidget>> widgets;
    lv_obj_t* tabContainer = tabContent[pageIndex];

    ESP_LOGI(TAG, "Creating %zu widget(s) for page %d",
            pageConfig.widgets.size(), pageIndex);

    for (const auto& widgetConfig : pageConfig.widgets)
    {
        // Validate widget position
        if (widgetConfig.x < 0 || widgetConfig.y < 0)
        {
            ESP_LOGW(TAG, "Invalid widget position: (%d,%d) - skipping",
                    widgetConfig.x, widgetConfig.y);
            continue;
        }

        if (widgetConfig.x + widgetConfig.w > gridInfo.gridWidth ||
            widgetConfig.y + widgetConfig.h > gridInfo.gridHeight)
        {
            ESP_LOGW(TAG, "Widget out of bounds: pos(%d,%d) size(%dx%d) in grid(%dx%d) - skipping",
                    widgetConfig.x, widgetConfig.y,
                    widgetConfig.w, widgetConfig.h,
                    gridInfo.gridWidth, gridInfo.gridHeight);
            continue;
        }

        // Create widget via factory
        auto widget = WidgetFactory::getInstance().createWidget(
            tabContainer, widgetConfig, gridInfo
        );

        if (widget)
        {
            widgets.push_back(std::move(widget));
            ESP_LOGI(TAG, "Created widget: type=%s, io_id=%s at (%d,%d)",
                    widgetConfig.type.c_str(), widgetConfig.io_id.c_str(),
                    widgetConfig.x, widgetConfig.y);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to create widget: type=%s, io_id=%s",
                    widgetConfig.type.c_str(), widgetConfig.io_id.c_str());
        }
    }

    pageWidgets.push_back(std::move(widgets));

    ESP_LOGI(TAG, "Page %d: created %zu widget(s)", pageIndex, pageWidgets[pageIndex].size());
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

    // Check for config updates
    if (state.config.pages_json != lastConfigJson && !state.config.pages_json.empty())
    {
        ESP_LOGI(TAG, "Config changed, recreating pages");
        lastConfigJson = state.config.pages_json;

        // Use display lock for UI updates
        if (HAL::getInstance().getDisplay().tryLock(100))
        {
            try
            {
                // Parse new config
                auto pagesConfig = state.config.getParsedPages();

                // Destroy old pages
                destroyPages();

                // Create new pages
                createPagesFromConfig(pagesConfig);
            }
            catch (const std::exception& e)
            {
                ESP_LOGE(TAG, "Failed to parse/create pages from config: %s", e.what());
            }

            HAL::getInstance().getDisplay().unlock();
        }
        else
        {
            ESP_LOGW(TAG, "Failed to acquire display lock for config update");
        }
    }
}

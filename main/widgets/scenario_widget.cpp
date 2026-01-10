#include <lvgl.h>
#include "scenario_widget.h"
#include "../theme.h"
#include "logging.h"
#include "images_generated.h"
#include "utils/color/color.h"

static const char* TAG = "widget.scenario";

ScenarioWidget::ScenarioWidget(lv_obj_t* parent,
                               const CalaosProtocol::WidgetConfig& config,
                               const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    iconImage(nullptr),
    nameLabel(nullptr),
    labelColorAnim(std::make_unique<smooth_ui_toolkit::color::AnimateRgb_t>()),
    bgColorAnim(std::make_unique<smooth_ui_toolkit::color::AnimateRgb_t>()),
    isAnimating(false),
    animationPhase(0),
    delayTimer(nullptr)
{
    ESP_LOGI(TAG, "Creating scenario widget: %s", config.io_id.c_str());
    createUI();
}

ScenarioWidget::~ScenarioWidget()
{
    ESP_LOGI(TAG, "Destroying scenario widget: %s", config.io_id.c_str());

    // Clean up timer if active
    if (delayTimer)
    {
        lv_timer_delete(delayTimer);
        delayTimer = nullptr;
    }
}

void ScenarioWidget::createUI()
{
    // Container styling - OFF state (same as LightSwitch OFF)
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // Make clickable
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(get(), pressEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(get(), clickEventCb, LV_EVENT_CLICKED, this);

    // Set flex layout for column
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Icon (static scenario icon)
    iconImage = lv_image_create(get());
    lv_image_set_src(iconImage, &icon_scenario);

    // Name label (blue color)
    nameLabel = lv_label_create(get());

    // Use IO name from state, or io_id as fallback
    const char* displayName = currentState.name.empty() ?
                             config.io_id.c_str() :
                             currentState.name.c_str();

    lv_label_set_text(nameLabel, displayName);
    lv_obj_set_style_text_font(nameLabel, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);

    // Enable text scrolling for long names
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, LV_PCT(100));

    // Initialize color animations to blue
    labelColorAnim->duration = 0.15f; // 150ms for bump
    labelColorAnim->begin();
    labelColorAnim->teleport(lv_color_to_u32(theme_color_blue));

    bgColorAnim->duration = 0.15f;
    bgColorAnim->begin();
    bgColorAnim->teleport(lv_color_to_u32(theme_color_widget_bg_off));
}

void ScenarioWidget::pressEventCb(lv_event_t* e)
{
    ScenarioWidget* widget = static_cast<ScenarioWidget*>(lv_event_get_user_data(e));
    if (widget)
    {
        widget->onPressed();
    }
}

void ScenarioWidget::onPressed()
{
    if (isAnimating)
        return;

    ESP_LOGI(TAG, "Scenario pressed: %s", config.io_id.c_str());

    // Set background to ON state immediately
    setBgColor(theme_color_widget_bg_on);
    setBorderColor(theme_color_widget_border_on);
}

void ScenarioWidget::clickEventCb(lv_event_t* e)
{
    ScenarioWidget* widget = static_cast<ScenarioWidget*>(lv_event_get_user_data(e));
    if (widget)
    {
        widget->onClicked();
    }
}

void ScenarioWidget::onClicked()
{
    if (isAnimating)
    {
        ESP_LOGW(TAG, "Ignoring click during animation");
        return;
    }

    ESP_LOGI(TAG, "Scenario clicked: %s", config.io_id.c_str());

    // Send action to server
    sendStateChange("true");

    // Start animation sequence from current ON state
    startAnimation();
}

void ScenarioWidget::startAnimation()
{
    isAnimating = true;
    animationPhase = 1;

    ESP_LOGI(TAG, "Starting animation phase 1: bump blue->yellow");

    // Phase 1: Bump blue->yellow (150ms)
    labelColorAnim->duration = 0.15f;
    labelColorAnim->begin();
    *labelColorAnim = lv_color_to_u32(theme_color_yellow);

    // Background stays ON, animate to OFF state during fade
    bgColorAnim->duration = 0.15f;
    bgColorAnim->begin();
    bgColorAnim->teleport(lv_color_to_u32(theme_color_widget_bg_on));
}

void ScenarioWidget::onBumpComplete()
{
    ESP_LOGI(TAG, "Bump animation complete, starting 400ms delay");

    animationPhase = 2;

    // Create timer for 400ms delay
    delayTimer = lv_timer_create(delayTimerCb, 400, this);
    lv_timer_set_repeat_count(delayTimer, 1); // Run once
}

void ScenarioWidget::delayTimerCb(lv_timer_t* timer)
{
    ScenarioWidget* widget = static_cast<ScenarioWidget*>(lv_timer_get_user_data(timer));
    if (widget)
    {
        widget->delayTimer = nullptr; // Timer will auto-delete
        widget->startFadeAnimation();
    }
}

void ScenarioWidget::startFadeAnimation()
{
    ESP_LOGI(TAG, "Starting animation phase 3: fade yellow->blue");

    animationPhase = 3;

    // Phase 2: Slow fade yellow->blue (600ms)
    labelColorAnim->duration = 0.6f;
    labelColorAnim->begin();
    *labelColorAnim = lv_color_to_u32(theme_color_blue);

    bgColorAnim->duration = 0.6f;
    bgColorAnim->begin();
    *bgColorAnim = lv_color_to_u32(theme_color_widget_bg_off);
}

void ScenarioWidget::onFadeComplete()
{
    ESP_LOGI(TAG, "Fade animation complete");

    isAnimating = false;
    animationPhase = 0;

    // Ensure widget is back to OFF state
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
}

void ScenarioWidget::render()
{
    if (!isAnimating)
        return;

    // Update animations
    labelColorAnim->update();

    // Apply label color
    lv_obj_set_style_text_color(nameLabel, lv_color_hex(labelColorAnim->toHex()), 0);

    // Apply background color only during fade phase (phase 3)
    if (animationPhase == 3)
    {
        bgColorAnim->update();
        uint32_t currentBgColor = bgColorAnim->toHex();
        lv_obj_set_style_bg_color(get(), lv_color_hex(currentBgColor), LV_PART_MAIN);

        // Animate border color along with background
        if (currentBgColor == lv_color_to_u32(theme_color_widget_bg_off))
            setBorderColor(theme_color_widget_border_off);
        else
            setBorderColor(theme_color_widget_border_on);
    }

    // Check for animation completion
    if (animationPhase == 1 && labelColorAnim->done())
    {
        onBumpComplete();
    }
    else if (animationPhase == 3 && labelColorAnim->done())
    {
        onFadeComplete();
    }
}

void ScenarioWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    // Scenarios don't receive state updates from server
    // Update name label if name changed
    if (!state.name.empty() && state.name != currentState.name)
    {
        ESP_LOGI(TAG, "Updating name for %s: %s", config.io_id.c_str(), state.name.c_str());
        currentState = state;
        lv_label_set_text(nameLabel, state.name.c_str());
    }
}

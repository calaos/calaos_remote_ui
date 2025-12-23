#include "light_switch_widget.h"
#include "../theme.h"
#include "logging.h"

static const char* TAG = "widget.light_switch";

LightSwitchWidget::LightSwitchWidget(lv_obj_t* parent,
                                   const CalaosProtocol::WidgetConfig& config,
                                   const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    iconLabel(nullptr),
    nameLabel(nullptr),
    updatingFromServer(false)
{
    ESP_LOGI(TAG, "Creating light switch widget: %s", config.io_id.c_str());
    createUI();

    // Set initial visual state
    bool isOn = (currentState.state == "true" || currentState.state == "1");
    updateVisualState(isOn);
}

LightSwitchWidget::~LightSwitchWidget()
{
    ESP_LOGI(TAG, "Destroying light switch widget: %s", config.io_id.c_str());
}

void LightSwitchWidget::createUI()
{
    // Container styling
    setBgColor(lv_color_make(0x30, 0x30, 0x30));  // Dark gray by default
    setBgOpa(LV_OPA_COVER);
    setRadius(10);
    setBorderWidth(2);
    setBorderColor(theme_color_blue);
    setPadding(16, 16, 16, 16);

    // Make clickable
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(get(), clickEventCb, LV_EVENT_CLICKED, this);

    // Icon (centered top-middle)
    iconLabel = lv_label_create(get());
    lv_label_set_text(iconLabel, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(iconLabel, LV_ALIGN_TOP_MID, 0, 20);

    // Name label (centered bottom)
    nameLabel = lv_label_create(get());

    // Use IO name from state, or io_id as fallback
    const char* displayName = currentState.name.empty() ?
                             config.io_id.c_str() :
                             currentState.name.c_str();

    lv_label_set_text(nameLabel, displayName);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_white, 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);

    // Enable text wrapping for long names
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nameLabel, lv_obj_get_width(get()) - 20);

    lv_obj_align(nameLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void LightSwitchWidget::updateVisualState(bool isOn)
{
    if (isOn)
    {
        // ON state: Blue background with opacity, yellow icon
        setBgColor(theme_color_blue);
        setBgOpa(LV_OPA_30);  // 30% opacity
        lv_obj_set_style_text_color(iconLabel, theme_color_yellow, 0);  // Yellow/orange icon when ON
    }
    else
    {
        // OFF state: Dark gray background, gray icon
        setBgColor(lv_color_make(0x30, 0x30, 0x30));
        setBgOpa(LV_OPA_COVER);
        lv_obj_set_style_text_color(iconLabel, lv_color_make(0x66, 0x66, 0x66), 0);  // Gray icon when OFF
    }
}

void LightSwitchWidget::clickEventCb(lv_event_t* e)
{
    LightSwitchWidget* widget = static_cast<LightSwitchWidget*>(lv_event_get_user_data(e));
    if (widget)
    {
        widget->onClicked();
    }
}

void LightSwitchWidget::onClicked()
{
    if (updatingFromServer)
    {
        ESP_LOGW(TAG, "Ignoring click during server update");
        return;
    }

    // Toggle state
    bool currentOn = (currentState.state == "true" || currentState.state == "1");
    bool newState = !currentOn;

    ESP_LOGI(TAG, "Light switch clicked: %s -> %s",
            currentOn ? "ON" : "OFF", newState ? "ON" : "OFF");

    sendStateChange(newState ? "true" : "false");
}

void LightSwitchWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    updatingFromServer = true;

    ESP_LOGI(TAG, "State update for %s: %s", config.io_id.c_str(), state.state.c_str());

    // Update current state
    currentState = state;

    // Update name label if name changed
    if (!state.name.empty())
    {
        lv_label_set_text(nameLabel, state.name.c_str());
    }

    // Update visual state
    bool isOn = (state.state == "true" || state.state == "1");
    updateVisualState(isOn);

    updatingFromServer = false;
}

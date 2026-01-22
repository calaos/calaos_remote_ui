#include "light_switch_wide_widget.h"
#include "../theme.h"
#include "logging.h"
#include "../image_sequence_animator.h"
#include "images_generated.h"

static const char* TAG = "widget.light_switch_wide";

LightSwitchWideWidget::LightSwitchWideWidget(lv_obj_t* parent,
                                             const CalaosProtocol::WidgetConfig& config,
                                             const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    topContainer(nullptr),
    iconImage(nullptr),
    textContainer(nullptr),
    nameLabel(nullptr),
    stateLabel(nullptr),
    slider(nullptr),
    lightAnimator(nullptr),
    updatingFromServer(false),
    wasOn(false)
{
    ESP_LOGI(TAG, "Creating light switch wide widget: %s (size %dx%d)",
             config.io_id.c_str(), config.w, config.h);
    createUI();

    // Set initial visual state
    bool isOn = parseIsOn(currentState.state);
    int brightness = getBrightness(currentState.state);
    updateVisualState(isOn);
    updateStateLabel(isOn, brightness);
    wasOn = isOn;

    // Update slider position for dimmers at startup
    if (slider && isDimmer())
        lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
}

LightSwitchWideWidget::~LightSwitchWideWidget()
{
    ESP_LOGI(TAG, "Destroying light switch wide widget: %s", config.io_id.c_str());
}

void LightSwitchWideWidget::createUI()
{
    // Container styling
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // Main container uses column flex layout
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Make clickable
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(get(), clickEventCb, LV_EVENT_CLICKED, this);

    // Top container: icon + text (horizontal row)
    topContainer = lv_obj_create(get());
    lv_obj_remove_style_all(topContainer);
    lv_obj_set_size(topContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topContainer, 16, 0);
    lv_obj_add_flag(topContainer, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Icon image (left side)
    iconImage = lv_image_create(topContainer);
    lv_obj_add_flag(iconImage, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Create animation configuration for light switch
    std::vector<const lv_image_dsc_t*> lightOnFrames = {
        &light_on_00, &light_on_01, &light_on_02, &light_on_03,
        &light_on_04, &light_on_05, &light_on_06, &light_on_07, &light_on_08
    };

    auto animConfig = ImageSequenceAnimator::createOneShot(
        lightOnFrames, nullptr, 40  // 40ms per frame
    );

    lightAnimator = std::make_unique<ImageSequenceAnimator>(
        iconImage, animConfig
    );

    lightAnimator->onComplete(
        [this]()
        {
            ESP_LOGI(TAG, "Light animation completed");
        }
    );

    // Text container: name + state (vertical column)
    textContainer = lv_obj_create(topContainer);
    lv_obj_remove_style_all(textContainer);
    lv_obj_set_flex_grow(textContainer, 1);
    lv_obj_set_height(textContainer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(textContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(textContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(textContainer, 4, 0);
    lv_obj_add_flag(textContainer, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Name label
    nameLabel = lv_label_create(textContainer);
    const char* displayName = currentState.name.empty() ?
                              config.io_id.c_str() :
                              currentState.name.c_str();
    lv_label_set_text(nameLabel, displayName);
    lv_obj_set_style_text_font(nameLabel, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, LV_PCT(100));
    lv_obj_add_flag(nameLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

    // State label
    stateLabel = lv_label_create(textContainer);
    lv_label_set_text(stateLabel, "Off");
    lv_obj_set_style_text_font(stateLabel, &roboto_regular_22, 0);
    lv_obj_set_style_text_color(stateLabel, theme_color_white, 0);
    lv_obj_add_flag(stateLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Slider (only for dimmers)
    if (isDimmer())
    {
        slider = lv_slider_create(get());
        lv_obj_set_width(slider, LV_PCT(100));
        lv_obj_set_height(slider, 14);
        lv_slider_set_range(slider, 0, 100);
        lv_slider_set_value(slider, 0, LV_ANIM_OFF);
        // Allow knob to overflow the thin track
        lv_obj_add_flag(slider, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        // Style the slider
        // Main track (background)
        lv_obj_set_style_bg_color(slider, theme_color_widget_bg_off, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 7, LV_PART_MAIN);
        lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(slider, theme_color_widget_border_off, LV_PART_MAIN);
        // Add horizontal padding so knob doesn't overflow parent
        lv_obj_set_style_pad_left(slider, 15, LV_PART_MAIN);
        lv_obj_set_style_pad_right(slider, 15, LV_PART_MAIN);

        // Indicator (filled part)
        lv_obj_set_style_bg_color(slider, theme_color_blue, LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 7, LV_PART_INDICATOR);

        // Knob
        lv_obj_set_style_bg_color(slider, theme_color_white, LV_PART_KNOB);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(slider, 4, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(slider, theme_color_blue, LV_PART_KNOB);
        lv_obj_set_style_shadow_opa(slider, 100, LV_PART_KNOB);

        // Add released event callback
        lv_obj_add_event_cb(slider, sliderReleasedCb, LV_EVENT_RELEASED, this);
    }
}

bool LightSwitchWideWidget::isDimmer() const
{
    return currentState.gui_type == "light_dimmer";
}

bool LightSwitchWideWidget::parseIsOn(const std::string& stateStr) const
{
    if (currentState.gui_type == "light_dimmer")
    {
        try
        {
            int value = std::stoi(stateStr);
            return value > 0;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return (stateStr == "true");
}

int LightSwitchWideWidget::getBrightness(const std::string& stateStr) const
{
    if (currentState.gui_type == "light_dimmer")
    {
        try
        {
            int value = std::stoi(stateStr);
            return std::max(0, std::min(100, value));
        }
        catch (const std::exception&)
        {
            return 0;
        }
    }
    // For non-dimmers, return 100 if on, 0 if off
    return (stateStr == "true") ? 100 : 0;
}

void LightSwitchWideWidget::updateVisualState(bool isOn)
{
    if (isOn)
    {
        setBgColor(theme_color_widget_bg_on);
        setBorderColor(theme_color_widget_border_on);

        // Only play animation when transitioning from OFF to ON
        if (!wasOn && lightAnimator)
            lightAnimator->play();
    }
    else
    {
        setBgColor(theme_color_widget_bg_off);
        setBorderColor(theme_color_widget_border_off);

        if (lightAnimator)
            lightAnimator->stop();

        lv_image_set_src(iconImage, &light_off);
    }

    wasOn = isOn;
}

void LightSwitchWideWidget::updateStateLabel(bool isOn, int brightness)
{
    if (!isOn)
    {
        lv_label_set_text(stateLabel, "Off");
    }
    else if (isDimmer())
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", brightness);
        lv_label_set_text(stateLabel, buf);
    }
    else
    {
        lv_label_set_text(stateLabel, "On");
    }
}

void LightSwitchWideWidget::clickEventCb(lv_event_t* e)
{
    LightSwitchWideWidget* widget = static_cast<LightSwitchWideWidget*>(lv_event_get_user_data(e));
    if (widget)
        widget->onClicked();
}

void LightSwitchWideWidget::sliderReleasedCb(lv_event_t* e)
{
    LightSwitchWideWidget* widget = static_cast<LightSwitchWideWidget*>(lv_event_get_user_data(e));
    if (widget)
        widget->onSliderReleased();
}

void LightSwitchWideWidget::onClicked()
{
    if (updatingFromServer)
    {
        ESP_LOGW(TAG, "Ignoring click during server update");
        return;
    }

    bool currentOn = parseIsOn(currentState.state);
    bool newState = !currentOn;

    ESP_LOGI(TAG, "Light switch wide clicked: %s -> %s",
             currentOn ? "ON" : "OFF", newState ? "ON" : "OFF");

    sendStateChange(newState ? "true" : "false");
}

void LightSwitchWideWidget::onSliderReleased()
{
    if (updatingFromServer || !slider)
    {
        ESP_LOGW(TAG, "Ignoring slider during server update");
        return;
    }

    int value = lv_slider_get_value(slider);
    ESP_LOGI(TAG, "Slider released with value: %d", value);

    // Send brightness value with "set XX" format
    char buf[16];
    snprintf(buf, sizeof(buf), "set %d", value);
    sendStateChange(buf);
}

void LightSwitchWideWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    updatingFromServer = true;

    ESP_LOGI(TAG, "State update for %s: %s", config.io_id.c_str(), state.state.c_str());

    currentState = state;

    // Update name label if name changed
    if (!state.name.empty())
        lv_label_set_text(nameLabel, state.name.c_str());

    // Update visual state
    bool isOn = parseIsOn(state.state);
    int brightness = getBrightness(state.state);
    updateVisualState(isOn);
    updateStateLabel(isOn, brightness);

    // Update slider position for dimmers
    if (slider && isDimmer())
        lv_slider_set_value(slider, brightness, LV_ANIM_ON);

    updatingFromServer = false;
}

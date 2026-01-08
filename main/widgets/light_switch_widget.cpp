#include "light_switch_widget.h"
#include "../theme.h"
#include "logging.h"
#include "../image_sequence_animator.h"
#include "images_generated.h"

static const char* TAG = "widget.light_switch";

LightSwitchWidget::LightSwitchWidget(lv_obj_t* parent,
                                   const CalaosProtocol::WidgetConfig& config,
                                   const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    iconImage(nullptr),
    nameLabel(nullptr),
    lightAnimator(nullptr),
    updatingFromServer(false)
{
    ESP_LOGI(TAG, "Creating light switch widget: %s", config.io_id.c_str());
    createUI();

    // Set initial visual state
    bool isOn = parseIsOn(currentState.state);
    updateVisualState(isOn);
}

LightSwitchWidget::~LightSwitchWidget()
{
    ESP_LOGI(TAG, "Destroying light switch widget: %s", config.io_id.c_str());
}

void LightSwitchWidget::createUI()
{
    // Container styling
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // Make clickable
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(get(), clickEventCb, LV_EVENT_CLICKED, this);

    // Icon (centered top-middle) - using animated image sequence
    iconImage = lv_image_create(get());
    lv_obj_align(iconImage, LV_ALIGN_TOP_MID, 0, 20);

    // Create animation configuration for light switch
    std::vector<const lv_image_dsc_t*> lightOnFrames = {
        &light_on_00, &light_on_01, &light_on_02, &light_on_03,
        &light_on_04, &light_on_05, &light_on_06, &light_on_07, &light_on_08
    };

    auto animConfig = ImageSequenceAnimator::createOneShot(
        lightOnFrames, nullptr, 40  // 40ms per frame, no static image
    );

    // Create the animator
    lightAnimator = std::make_unique<ImageSequenceAnimator>(
        iconImage, animConfig
    );

    // Set up completion callback
    lightAnimator->onComplete([this]() {
        ESP_LOGI(TAG, "Light animation completed");
    });

    // Name label (centered bottom)
    nameLabel = lv_label_create(get());

    // Use IO name from state, or io_id as fallback
    const char* displayName = currentState.name.empty() ?
                             config.io_id.c_str() :
                             currentState.name.c_str();

    lv_label_set_text(nameLabel, displayName);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);

    // Enable text scrolling for long names
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, LV_PCT(100));

    lv_obj_align(nameLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void LightSwitchWidget::updateVisualState(bool isOn)
{
    if (isOn)
    {
        // ON state: Blue background with opacity, animated icon
        setBgColor(theme_color_widget_bg_on);
        setBorderColor(theme_color_widget_border_on);

        // Start light animation sequence (will end on light_on_08)
        if (lightAnimator) {
            lightAnimator->play();
        }
    }
    else
    {
        // OFF state: Dark gray background, static off icon
        setBgColor(theme_color_widget_bg_off);
        setBorderColor(theme_color_widget_border_off);

        // Stop animation and directly show light_off image
        if (lightAnimator) {
            lightAnimator->stop();
        }
        // Directly set the OFF image
        lv_image_set_src(iconImage, &light_off);
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

    // Toggle state using parseIsOn to handle both light and light_dimmer
    bool currentOn = parseIsOn(currentState.state);
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
    bool isOn = parseIsOn(state.state);
    updateVisualState(isOn);

    updatingFromServer = false;
}

bool LightSwitchWidget::parseIsOn(const std::string& stateStr) const
{
    // Parse based on gui_type
    if (currentState.gui_type == "light_dimmer")
    {
        // light_dimmer: integer percentage (0-100), ON if > 0
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

    // gui_type="light" or others: boolean "true"/"false"
    return (stateStr == "true");
}

#include "shutter_widget.h"
#include "../theme.h"
#include "logging.h"
#include "images_generated.h"

static const char* TAG = "widget.shutter";

ShutterWidget::ShutterWidget(lv_obj_t* parent,
                             const CalaosProtocol::WidgetConfig& config,
                             const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    iconImage(nullptr),
    nameLabel(nullptr),
    shutterContainer(nullptr),
    shutterFrame(nullptr),
    shutterMoving(nullptr)
{
    ESP_LOGI(TAG, "Creating shutter widget: %s (gui_type=%s)",
             config.io_id.c_str(), currentState.gui_type.c_str());
    createUI();

    // Set initial visual state
    if (isSmartShutter())
    {
        ShutterSmartState smartState = parseShutterSmartState(currentState.state);
        updateSmartVisual(smartState);
    }
    else
    {
        bool open = parseIsOpen(currentState.state);
        updateSimpleVisual(open);
    }
}

ShutterWidget::~ShutterWidget()
{
    ESP_LOGI(TAG, "Destroying shutter widget: %s", config.io_id.c_str());
}

bool ShutterWidget::isSmartShutter() const
{
    return isShutterSmart(currentState.gui_type);
}

bool ShutterWidget::parseIsOpen(const std::string& stateStr) const
{
    return parseShutterIsOpen(stateStr);
}

void ShutterWidget::createUI()
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

    if (isSmartShutter())
    {
        // Smart shutter: animated visual with part_shutter + part_shutter2
        ShutterVisualResult visual = createShutterSmartVisual(
            get(), &part_shutter, &part_shutter2, true);
        shutterContainer = visual.container;
        shutterFrame = visual.frame;
        shutterMoving = visual.moving;
        lv_obj_align(shutterContainer, LV_ALIGN_TOP_MID, 0, 0);
    }
    else
    {
        // Simple shutter: static icon
        iconImage = lv_image_create(get());
        lv_image_set_src(iconImage, &icon_shutter_off);
        lv_obj_align(iconImage, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_add_flag(iconImage, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    // Name label (centered bottom)
    nameLabel = lv_label_create(get());
    lv_label_set_text(nameLabel, getDisplayName().c_str());
    lv_obj_set_style_text_font(nameLabel, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);

    // Enable text scrolling for long names
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, LV_PCT(100));

    lv_obj_align(nameLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ShutterWidget::updateSimpleVisual(bool isOpen)
{
    if (isOpen)
    {
        setBgColor(theme_color_widget_bg_on);
        setBorderColor(theme_color_widget_border_on);
        if (iconImage)
            lv_image_set_src(iconImage, &icon_shutter_on);
    }
    else
    {
        setBgColor(theme_color_widget_bg_off);
        setBorderColor(theme_color_widget_border_off);
        if (iconImage)
            lv_image_set_src(iconImage, &icon_shutter_off);
    }
}

void ShutterWidget::updateSmartVisual(const ShutterSmartState& smartState)
{
    // Update background color based on open/closed state
    if (smartState.isOpen)
    {
        setBgColor(theme_color_widget_bg_on);
        setBorderColor(theme_color_widget_border_on);
    }
    else
    {
        setBgColor(theme_color_widget_bg_off);
        setBorderColor(theme_color_widget_border_off);
    }

    // Update moving part position
    updateShutterMovingPosition(shutterMoving, smartState.percent);
}

void ShutterWidget::clickEventCb(lv_event_t* e)
{
    ShutterWidget* widget = static_cast<ShutterWidget*>(lv_event_get_user_data(e));
    if (widget)
        widget->onClicked();
}

void ShutterWidget::onClicked()
{
    ESP_LOGI(TAG, "Shutter clicked: %s", config.io_id.c_str());
    sendStateChange("toggle");
}

void ShutterWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    ESP_LOGI(TAG, "State update for %s: %s", config.io_id.c_str(), state.state.c_str());

    currentState = state;

    // Update name label
    lv_label_set_text(nameLabel, getDisplayName(state).c_str());

    if (isSmartShutter())
    {
        ShutterSmartState smartState = parseShutterSmartState(state.state);
        updateSmartVisual(smartState);
    }
    else
    {
        bool open = parseIsOpen(state.state);
        updateSimpleVisual(open);
    }
}

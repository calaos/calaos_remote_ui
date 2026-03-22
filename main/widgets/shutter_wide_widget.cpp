#include "shutter_wide_widget.h"
#include "../theme.h"
#include "logging.h"
#include "images_generated.h"

static const char* TAG = "widget.shutter_wide";

ShutterWideWidget::ShutterWideWidget(lv_obj_t* parent,
                                     const CalaosProtocol::WidgetConfig& config,
                                     const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    topContainer(nullptr),
    iconImage(nullptr),
    nameLabel(nullptr),
    shutterContainer(nullptr),
    shutterFrame(nullptr),
    shutterMoving(nullptr),
    btnUp(nullptr),
    btnDown(nullptr)
{
    ESP_LOGI(TAG, "Creating shutter wide widget: %s (size %dx%d, gui_type=%s)",
             config.io_id.c_str(), config.w, config.h, currentState.gui_type.c_str());
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

ShutterWideWidget::~ShutterWideWidget()
{
    ESP_LOGI(TAG, "Destroying shutter wide widget: %s", config.io_id.c_str());
}

bool ShutterWideWidget::isSmartShutter() const
{
    return isShutterSmart(currentState.gui_type);
}

bool ShutterWideWidget::parseIsOpen(const std::string& stateStr) const
{
    return parseShutterIsOpen(stateStr);
}

void ShutterWideWidget::createUI()
{
    // Container styling
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // NOT clickable - only buttons are clickable
    // Use column flex: top row + name label
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Top container: icon + buttons (horizontal row)
    topContainer = lv_obj_create(get());
    lv_obj_remove_style_all(topContainer);
    lv_obj_set_size(topContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topContainer, 8, 0);

    if (isSmartShutter())
    {
        // Smart shutter: animated visual with part_shutter + part_shutter2
        ShutterVisualResult visual = createShutterSmartVisual(
            topContainer, &part_shutter, &part_shutter2);
        shutterContainer = visual.container;
        shutterFrame = visual.frame;
        shutterMoving = visual.moving;
    }
    else
    {
        // Simple shutter: static icon
        iconImage = lv_image_create(topContainer);
        lv_image_set_src(iconImage, &icon_shutter_off);
    }

    // Buttons row (right side)
    createButtons(topContainer);

    // Name label (bottom)
    nameLabel = lv_label_create(get());
    lv_label_set_text(nameLabel, getDisplayName().c_str());
    lv_obj_set_style_text_font(nameLabel, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, LV_PCT(100));
}

void ShutterWideWidget::createButtons(lv_obj_t* parent)
{
    // Button row container (horizontal layout as per QML RowLayout)
    lv_obj_t* btnContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(btnContainer);
    lv_obj_set_size(btnContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btnContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnContainer, 4, 0);

    btnUp = createShutterButton(btnContainer, &icon_shutter_up,
                                 theme_color_widget_bg_on, upButtonCb, this);
    btnDown = createShutterButton(btnContainer, &icon_shutter_down,
                                   theme_color_widget_bg_on, downButtonCb, this);
}

void ShutterWideWidget::updateSimpleVisual(bool isOpen)
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

void ShutterWideWidget::updateSmartVisual(const ShutterSmartState& smartState)
{
    // Update background color
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

void ShutterWideWidget::upButtonCb(lv_event_t* e)
{
    ShutterWideWidget* widget = static_cast<ShutterWideWidget*>(lv_event_get_user_data(e));
    if (widget)
    {
        ESP_LOGI(TAG, "Shutter UP: %s", widget->config.io_id.c_str());
        widget->sendStateChange("up");
    }
}

void ShutterWideWidget::downButtonCb(lv_event_t* e)
{
    ShutterWideWidget* widget = static_cast<ShutterWideWidget*>(lv_event_get_user_data(e));
    if (widget)
    {
        ESP_LOGI(TAG, "Shutter DOWN: %s", widget->config.io_id.c_str());
        widget->sendStateChange("down");
    }
}

void ShutterWideWidget::onStateUpdate(const CalaosProtocol::IoState& state)
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

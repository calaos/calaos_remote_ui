#include "temperature_widget.h"
#include "../theme.h"
#include "logging.h"
#include "images_generated.h"
#include <sstream>
#include <iomanip>
#include <cstdlib>

static const char* TAG = "widget.temperature";

TemperatureWidget::TemperatureWidget(lv_obj_t* parent,
                                   const CalaosProtocol::WidgetConfig& config,
                                   const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    iconImage(nullptr),
    tempLabel(nullptr),
    nameLabel(nullptr)
{
    ESP_LOGI(TAG, "Creating temperature widget: %s", config.io_id.c_str());
    createUI();

    // Set initial temperature display
    std::string tempStr = formatTemperature(currentState.state);
    lv_label_set_text(tempLabel, tempStr.c_str());
}

void TemperatureWidget::createUI()
{
    // Container styling - same as LightSwitch OFF state
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // Not clickable (read-only widget)
    lv_obj_clear_flag(get(), LV_OBJ_FLAG_CLICKABLE);

    // Column flex layout, centered
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Icon at top (icon_temp)
    iconImage = lv_image_create(get());
    lv_image_set_src(iconImage, &icon_temp);

    // Temperature label (yellow color)
    tempLabel = lv_label_create(get());
    lv_label_set_text(tempLabel, "-- °C");
    lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(tempLabel, theme_color_yellow, 0);

    // Name label at bottom (blue color)
    nameLabel = lv_label_create(get());
    const char* displayName = currentState.name.empty() ?
                             config.io_id.c_str() :
                             currentState.name.c_str();
    lv_label_set_text(nameLabel, displayName);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(nameLabel, theme_color_blue, 0);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(nameLabel, lv_pct(90));
}

void TemperatureWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    ESP_LOGI(TAG, "Temperature widget state update: %s = %s",
             state.id.c_str(), state.state.c_str());

    // Update temperature display
    std::string tempStr = formatTemperature(state.state);
    lv_label_set_text(tempLabel, tempStr.c_str());

    // Update name if changed
    const char* displayName = state.name.empty() ?
                             config.io_id.c_str() :
                             state.name.c_str();
    lv_label_set_text(nameLabel, displayName);
}

std::string TemperatureWidget::formatTemperature(const std::string& tempStr)
{
    if (tempStr.empty())
    {
        return "-- °C";
    }

    // Try to parse as float
    char* endPtr = nullptr;
    double tempValue = std::strtod(tempStr.c_str(), &endPtr);

    // Check if parsing was successful (endPtr should point to end of string)
    if (endPtr == tempStr.c_str() || *endPtr != '\0')
    {
        ESP_LOGW(TAG, "Invalid temperature value: %s", tempStr.c_str());
        return "-- °C";
    }

    // Format with maximum 2 decimal places
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << tempValue;
    std::string formatted = oss.str();

    // Remove trailing zeros
    while (!formatted.empty() && formatted.back() == '0')
    {
        formatted.pop_back();
    }

    // Remove decimal point if it's the last character
    if (!formatted.empty() && formatted.back() == '.')
    {
        formatted.pop_back();
    }

    return formatted + "°C";
}

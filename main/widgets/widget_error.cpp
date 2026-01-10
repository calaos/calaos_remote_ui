#include "widget_error.h"
#include "../theme.h"
#include "logging.h"
#include <sstream>

static const char* TAG = "widget.error";

WidgetError::WidgetError(lv_obj_t* parent,
                        const CalaosProtocol::WidgetConfig& config,
                        const GridLayoutInfo& gridInfo,
                        const std::string& errorMessage):
    CalaosWidget(parent, config, gridInfo),
    errorMessage(errorMessage),
    warningIcon(nullptr),
    errorLabel(nullptr),
    typeLabel(nullptr),
    sizeLabel(nullptr)
{
    ESP_LOGW(TAG, "Creating error widget for %s: %s", config.io_id.c_str(), errorMessage.c_str());
    createUI();
}

WidgetError::~WidgetError()
{
    ESP_LOGI(TAG, "Destroying error widget: %s", config.io_id.c_str());
}

void WidgetError::createUI()
{
    // Dark red background to indicate error
    setBgColor(lv_color_make(0x40, 0x20, 0x20));
    setBgOpa(LV_OPA_COVER);
    setRadius(8);
    setBorderWidth(2);
    setBorderColor(theme_color_red);
    setPadding(8, 8, 8, 8);

    // Warning icon at top
    warningIcon = lv_label_create(get());
    lv_label_set_text(warningIcon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warningIcon, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(warningIcon, theme_color_red, 0);
    lv_obj_align(warningIcon, LV_ALIGN_TOP_MID, 0, 10);

    // "Unsupported" text
    errorLabel = lv_label_create(get());
    lv_label_set_text(errorLabel, "Unsupported");
    lv_obj_set_style_text_font(errorLabel, &roboto_medium_28, 0);
    lv_obj_set_style_text_color(errorLabel, theme_color_white, 0);
    lv_obj_align(errorLabel, LV_ALIGN_CENTER, 0, -20);

    // Widget type
    typeLabel = lv_label_create(get());
    lv_label_set_text(typeLabel, config.type.c_str());
    lv_obj_set_style_text_font(typeLabel, &roboto_regular_24, 0);
    lv_obj_set_style_text_color(typeLabel, theme_color_yellow, 0);
    lv_obj_align(typeLabel, LV_ALIGN_CENTER, 0, 15);

    // Widget size
    sizeLabel = lv_label_create(get());
    std::ostringstream oss;
    oss << config.w << "x" << config.h;
    lv_label_set_text(sizeLabel, oss.str().c_str());
    lv_obj_set_style_text_font(sizeLabel, &roboto_light_22, 0);
    lv_obj_set_style_text_color(sizeLabel, lv_color_make(0xAA, 0xAA, 0xAA), 0);
    lv_obj_align(sizeLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void WidgetError::onStateUpdate(const CalaosProtocol::IoState& state)
{
    // Error widget doesn't respond to state updates
    // It's just a visual placeholder
}

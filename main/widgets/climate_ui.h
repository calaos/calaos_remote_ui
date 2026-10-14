#pragma once

#include "climate_present.h"
#include "lvgl.h"
#include <string>
#include <vector>

// LVGL pieces shared by the small Climate widgets and the full-screen view
namespace climate_ui
{

lv_color_t toneColor(climate::Tone tone);
const lv_image_dsc_t* iconImage(climate::Icon icon);

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow);
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
int32_t textWidth(const char* text, const lv_font_t* font);
// Largest font whose text fits in width
const lv_font_t* fit(const std::vector<const lv_font_t*>& fonts, const std::string& text, int width);

void setText(lv_obj_t* label, const std::string& text);
void setVisible(lv_obj_t* obj, bool visible);
void setEnabled(lv_obj_t* obj, bool enabled);

// White artwork recoloured to the tone
lv_obj_t* icon(lv_obj_t* parent, climate::Icon icon, lv_color_t color);
void setIcon(lv_obj_t* image, climate::Icon icon, lv_color_t color);

// Strokes drawn with lines: the panel's fonts have no chevron or cross
lv_obj_t* chevronRight(lv_obj_t* parent, lv_color_t color);
lv_obj_t* chevronDown(lv_obj_t* parent, lv_color_t color);
lv_obj_t* cross(lv_obj_t* parent, lv_color_t color);
lv_obj_t* dots(lv_obj_t* parent, lv_color_t color);

// 3 px bar filled to a fraction, the relay wait
lv_obj_t* gauge(lv_obj_t* parent, int width, lv_color_t color);
void setGauge(lv_obj_t* gauge, double fraction);

// Rounded square with a minus, or a plus, drawn as bars
lv_obj_t* stepButton(lv_obj_t* parent, bool plus, int size);

// Choice button of the popups
lv_obj_t* choiceButton(lv_obj_t* parent, int height);
void setChoiceActive(lv_obj_t* button, bool active);

lv_style_selector_t when(lv_state_t state);

} // namespace climate_ui

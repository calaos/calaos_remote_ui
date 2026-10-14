#include "climate_ui.h"
#include "../theme.h"
#include "images_generated.h"
#include <algorithm>

namespace climate_ui
{

using climate::Icon;
using climate::Tone;

lv_style_selector_t when(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(state);
}

Surface surfaceFor(climate::Backdrop backdrop)
{
    switch (backdrop)
    {
        case climate::Backdrop::Heat: return {theme_color_widget_bg_heat, theme_color_yellow};
        case climate::Backdrop::Cool: return {theme_color_widget_bg_on, theme_color_blue};
        case climate::Backdrop::Off: break;
    }
    return {theme_color_widget_bg_off, theme_color_widget_border_off};
}

lv_color_t toneColor(Tone tone)
{
    switch (tone)
    {
        case Tone::Heat: return theme_color_yellow;
        case Tone::Cool: return theme_color_blue;
        case Tone::Wait: return theme_color_amber;
        case Tone::Alert: return theme_color_red;
        case Tone::White: return theme_color_white;
        case Tone::Muted: return theme_color_muted;
        case Tone::Dim: return theme_color_dim;
    }
    return theme_color_muted;
}

const lv_image_dsc_t* iconImage(Icon icon)
{
    switch (icon)
    {
        case Icon::Power: return &climate_power;
        case Icon::Flame: return &climate_flame;
        case Icon::Snow: return &climate_snow;
        case Icon::Auto: return &climate_auto;
        case Icon::Fan: return &climate_fan;
        case Icon::Drop: return &climate_drop;
        case Icon::Help: return &climate_help;
        case Icon::Clock: return &climate_clock;
        case Icon::Unlink: return &climate_unlink;
    }
    return &climate_help;
}

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

int32_t textWidth(const char* text, const lv_font_t* font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int lineCount(const std::string& text, const lv_font_t* font, int maxWidth)
{
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), font, 0, 0, maxWidth, LV_TEXT_FLAG_NONE);
    return std::max(1, static_cast<int>(size.y / std::max(1, static_cast<int>(lv_font_get_line_height(font)))));
}

const lv_font_t* fit(const std::vector<const lv_font_t*>& fonts, const std::string& text, int width)
{
    for (const lv_font_t* f : fonts)
        if (textWidth(text.c_str(), f) <= width)
            return f;
    return fonts.back();
}

void setText(lv_obj_t* l, const std::string& text)
{
    if (l && text != lv_label_get_text(l))
        lv_label_set_text(l, text.c_str());
}

void setVisible(lv_obj_t* o, bool visible)
{
    if (!o)
        return;
    if (visible)
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void setEnabled(lv_obj_t* o, bool enabled)
{
    if (!o)
        return;
    if (enabled)
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    else
        lv_obj_add_state(o, LV_STATE_DISABLED);
}

void setIcon(lv_obj_t* image, Icon ic, lv_color_t color)
{
    lv_image_set_src(image, iconImage(ic));
    lv_obj_set_style_image_recolor(image, color, 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
}

lv_obj_t* icon(lv_obj_t* parent, Icon ic, lv_color_t color)
{
    lv_obj_t* image = lv_image_create(parent);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    setIcon(image, ic, color);
    return image;
}

namespace
{

// A line keeps the pointer it is given
const lv_point_precise_t kChevronRight[] = {{0, 0}, {6, 6}, {0, 12}};
const lv_point_precise_t kChevronDown[] = {{0, 0}, {6, 6}, {12, 0}};
const lv_point_precise_t kCrossA[] = {{0, 0}, {14, 14}};
const lv_point_precise_t kCrossB[] = {{0, 14}, {14, 0}};

lv_obj_t* stroke(lv_obj_t* parent, const lv_point_precise_t* points, uint32_t count, lv_color_t color,
                 int width)
{
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_line_set_points(l, points, count);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

lv_obj_t* holder(lv_obj_t* parent, int w, int h)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, w, h);
    return o;
}

} // namespace

lv_obj_t* chevronRight(lv_obj_t* parent, lv_color_t color)
{
    lv_obj_t* h = holder(parent, 10, 16);
    lv_obj_center(stroke(h, kChevronRight, 3, color, 3));
    return h;
}

lv_obj_t* chevronDown(lv_obj_t* parent, lv_color_t color)
{
    lv_obj_t* h = holder(parent, 18, 12);
    lv_obj_center(stroke(h, kChevronDown, 3, color, 3));
    return h;
}

lv_obj_t* cross(lv_obj_t* parent, lv_color_t color)
{
    lv_obj_t* h = holder(parent, 20, 20);
    lv_obj_center(stroke(h, kCrossA, 2, color, 3));
    lv_obj_center(stroke(h, kCrossB, 2, color, 3));
    return h;
}

lv_obj_t* dots(lv_obj_t* parent, lv_color_t color)
{
    lv_obj_t* h = holder(parent, 26, 6);
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t* d = lv_obj_create(h);
        lv_obj_remove_style_all(d);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_pos(d, i * 10, 0);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, color, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    return h;
}

lv_obj_t* gauge(lv_obj_t* parent, int width, lv_color_t color)
{
    lv_obj_t* track = holder(parent, width, 3);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_set_style_bg_color(track, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_20, 0);

    lv_obj_t* fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(fill, 0, 3);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_bg_color(fill, color, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    return track;
}

void setGauge(lv_obj_t* g, double fraction)
{
    if (!g)
        return;
    lv_obj_t* fill = lv_obj_get_child(g, 0);
    const int w = static_cast<int>(lv_obj_get_width(g) * std::clamp(fraction, 0.0, 1.0));
    if (lv_obj_get_width(fill) != w)
        lv_obj_set_width(fill, w);
}

lv_obj_t* stepButton(lv_obj_t* parent, bool plus, int size)
{
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, theme_color_widget_bg_off, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, theme_color_widget_border_off, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_bg_color(b, theme_color_widget_bg_on, when(LV_STATE_PRESSED));
    lv_obj_set_style_border_color(b, theme_color_blue, when(LV_STATE_PRESSED));
    lv_obj_set_style_opa(b, LV_OPA_40, when(LV_STATE_DISABLED));

    // Bars, not glyphs: Roboto's hyphen is too short to read as a minus
    const int length = size * 2 / 5;
    for (int i = 0; i < (plus ? 2 : 1); i++)
    {
        lv_obj_t* bar = lv_obj_create(b);
        lv_obj_remove_style_all(bar);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(bar, i ? 3 : length, i ? length : 3);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_color(bar, theme_color_white, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_center(bar);
    }
    return b;
}

lv_obj_t* choiceButton(lv_obj_t* parent, int height)
{
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, height);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 10, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, theme_color_black, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, theme_color_widget_border_off, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_bg_color(b, theme_color_widget_bg_on, when(LV_STATE_PRESSED));
    lv_obj_set_style_opa(b, LV_OPA_40, when(LV_STATE_DISABLED));
    return b;
}

void setChoiceActive(lv_obj_t* button, bool active)
{
    lv_obj_set_style_bg_color(button, active ? theme_color_widget_bg_on : theme_color_black, 0);
    lv_obj_set_style_border_color(button, active ? theme_color_blue : theme_color_widget_border_off, 0);
    const lv_color_t ink = active ? theme_color_blue : theme_color_white;
    for (uint32_t i = 0; i < lv_obj_get_child_count(button); i++)
    {
        lv_obj_t* c = lv_obj_get_child(button, i);
        if (lv_obj_check_type(c, &lv_label_class))
            lv_obj_set_style_text_color(c, ink, 0);
        else if (lv_obj_check_type(c, &lv_image_class))
            lv_obj_set_style_image_recolor(c, ink, 0);
    }
}

} // namespace climate_ui

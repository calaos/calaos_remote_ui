#include "climate_dial.h"
#include "climate_ui.h"
#include "../theme.h"
#include <algorithm>
#include <cmath>

using namespace climate;

ClimateDial::ClimateDial(lv_obj_t* parent, int diameter, int stroke, int knobRadius, bool miniMark,
                         lv_color_t separator):
    diameter_(diameter),
    stroke_(stroke),
    knobRadius_(knobRadius),
    miniMark_(miniMark)
{
    height_ = dialHeight(diameter, stroke, knobRadius);
    const int pad = std::max(knobRadius + 2, stroke / 2) + 1;
    centre_ = diameter / 2.0;
    radius_ = centre_ - pad;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(root_, diameter, height_);

    // The arc object is the circle through the middle of the track, widened by half a stroke
    const int size = static_cast<int>(std::lround(2 * radius_ + stroke));
    arc_ = lv_arc_create(root_);
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc_, size, size);
    lv_obj_set_pos(arc_, static_cast<int>(std::lround(centre_ - size / 2.0)),
                   static_cast<int>(std::lround(centre_ - size / 2.0)));
    lv_arc_set_rotation(arc_, 0);
    lv_arc_set_bg_angles(arc_, kDialStart, (kDialStart + kDialSpan) % 360);
    lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, 0, 0);
    lv_obj_set_style_arc_width(arc_, stroke, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, lv_color_make(0x2A, 0x2A, 0x2A), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, stroke, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, theme_color_muted, LV_PART_INDICATOR);

    tick_ = lv_line_create(root_);
    lv_obj_remove_flag(tick_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(tick_, 0, 0);
    lv_line_set_points_mutable(tick_, tickPoints_, 2);
    lv_obj_set_style_line_width(tick_, 3, 0);
    lv_obj_set_style_line_color(tick_, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(tick_, true, 0);

    dot_ = lv_obj_create(root_);
    lv_obj_remove_style_all(dot_);
    lv_obj_remove_flag(dot_, LV_OBJ_FLAG_CLICKABLE);
    const int dotSize = std::max(4, static_cast<int>(stroke * 0.9));
    lv_obj_set_size(dot_, dotSize, dotSize);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);

    lowKnob_ = knob(root_);
    highKnob_ = knob(root_);
    setSeparator(separator);

    // The texts of the dial sit on its centre, inside the track
    const int inner = static_cast<int>(2 * radius_ - 2 * stroke);
    inner_ = inner;
    content_ = climate_ui::box(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(content_, inner, inner);
    lv_obj_set_pos(content_, static_cast<int>(centre_ - inner / 2.0), static_cast<int>(centre_ - inner / 2.0));
    lv_obj_set_style_pad_row(content_, 2, 0);
}

lv_obj_t* ClimateDial::knob(lv_obj_t* parent)
{
    lv_obj_t* k = lv_obj_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_remove_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(k, 2 * knobRadius_, 2 * knobRadius_);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k, miniMark_ ? 1 : 2, 0);
    return k;
}

void ClimateDial::setSeparator(lv_color_t color)
{
    lv_obj_set_style_border_color(lowKnob_, color, 0);
    lv_obj_set_style_border_color(highKnob_, color, 0);
}

void ClimateDial::place(lv_obj_t* obj, double fraction, int radius)
{
    const double a = (kDialStart + kDialSpan * fraction) * M_PI / 180.0;
    lv_obj_set_pos(obj, static_cast<int>(std::lround(centre_ + radius_ * std::cos(a) - radius)),
                   static_cast<int>(std::lround(centre_ + radius_ * std::sin(a) - radius)));
}

void ClimateDial::set(const Dial& d)
{
    using climate_ui::setVisible;
    using climate_ui::toneColor;

    const auto angles = [](double from, double to, int* start, int* end)
    {
        *start = dialAngle(from);
        *end = dialAngle(to);
        // The arc has nothing to draw between equal angles
        if (*end == *start)
            *end = (*start + 1) % 360;
    };

    const bool single = d.to.has_value();
    const bool range = d.low.has_value() && d.high.has_value();

    lv_obj_set_style_arc_opa(arc_, single || range ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    if (single || range)
    {
        int start, end;
        if (single)
            angles(0.0, *d.to, &start, &end);
        else
            angles(*d.low, *d.high, &start, &end);
        lv_arc_set_angles(arc_, start, end);
        lv_obj_set_style_arc_color(arc_, toneColor(d.fill), LV_PART_INDICATOR);
    }

    setVisible(lowKnob_, single || range);
    setVisible(highKnob_, range);
    if (single)
    {
        place(lowKnob_, *d.to, knobRadius_);
        lv_obj_set_style_bg_color(lowKnob_, toneColor(d.knob), 0);
    }
    else if (range)
    {
        place(lowKnob_, *d.low, knobRadius_);
        place(highKnob_, *d.high, knobRadius_);
        lv_obj_set_style_bg_color(lowKnob_, toneColor(d.lowKnob), 0);
        lv_obj_set_style_bg_color(highKnob_, toneColor(d.highKnob), 0);
    }

    setVisible(tick_, d.room.has_value() && !miniMark_);
    setVisible(dot_, d.room.has_value() && miniMark_);
    if (d.room)
    {
        if (miniMark_)
            place(dot_, *d.room, lv_obj_get_width(dot_) / 2);
        else
        {
            const double a = (kDialStart + kDialSpan * *d.room) * M_PI / 180.0;
            const double inner = radius_ - stroke_ * 1.3, outer = radius_ + stroke_ * 1.3;
            tickPoints_[0] = {static_cast<lv_value_precise_t>(centre_ + inner * std::cos(a)),
                              static_cast<lv_value_precise_t>(centre_ + inner * std::sin(a))};
            tickPoints_[1] = {static_cast<lv_value_precise_t>(centre_ + outer * std::cos(a)),
                              static_cast<lv_value_precise_t>(centre_ + outer * std::sin(a))};
            lv_obj_set_size(tick_, diameter_, height_);
            lv_obj_invalidate(tick_);
        }
    }
}

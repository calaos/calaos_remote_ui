#pragma once

#include "climate_present.h"
#include "lvgl.h"

// The 300 degree dial: a neutral track, the lit part up to the setpoint (or
// between the two handles), knobs, and the room temperature as a mark.
class ClimateDial
{
public:
    // miniMark: the room reading is a dot (small dials) instead of a tick across the track.
    // separator is the colour of what the dial sits on, for the ring around each knob.
    ClimateDial(lv_obj_t* parent, int diameter, int stroke, int knobRadius, bool miniMark,
                lv_color_t separator);

    void set(const climate::Dial& dial);
    void setSeparator(lv_color_t color);

    lv_obj_t* get() const { return root_; }
    // Centred in the circle, for the texts inside it
    lv_obj_t* content() const { return content_; }
    int height() const { return height_; }

private:
    lv_obj_t* knob(lv_obj_t* parent);
    void place(lv_obj_t* obj, double fraction, int radius);

    lv_obj_t* root_;
    lv_obj_t* arc_;
    lv_obj_t* content_;
    lv_obj_t* lowKnob_;
    lv_obj_t* highKnob_;
    lv_obj_t* tick_;
    lv_obj_t* dot_;
    lv_point_precise_t tickPoints_[2] = {{0, 0}, {0, 0}};

    int diameter_;
    int stroke_;
    int knobRadius_;
    bool miniMark_;
    int height_;
    double centre_;
    double radius_;     // of the middle of the track
};

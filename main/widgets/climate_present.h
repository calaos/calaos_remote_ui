#pragma once

#include "climate_logic.h"
#include <cstdint>
#include <optional>
#include <string>

// What the small Climate widgets and the full-screen view show, decided without
// LVGL so the host tests cover it. Spec: docs/devices/UI_CLIMATE.md.
namespace climate
{

// Colours by role; the widgets map them to the theme
enum class Tone { Heat, Cool, Wait, Alert, White, Muted, Dim };

enum class Icon { Power, Flame, Snow, Auto, Fan, Drop, Help, Clock, Unlink };

// The widget is lit when the device acts, in the colour of what it does
enum class Backdrop { Off, Cool, Heat };

enum class Phase { Offline, Unknown, Off, Fault, Waiting, Acting, Idle, Mode };

struct Status
{
    Phase phase = Phase::Unknown;
    Tone tone = Tone::Muted;
    Icon icon = Icon::Help;
    std::string text;           // "Démarre dans 2 min 20"
    std::string shortText;      // 1x1: "Attente"
    std::string widgetText;     // 2x1, 1x2: "Démarre dans 3 min"
    Backdrop backdrop = Backdrop::Off;
    bool canSet = false;        // offline and unknown devices take no setpoint
};

// remaining is the local countdown, in seconds; <= 0 when there is none.
// An action wins over the bare mode only when the device reports one.
Status statusOf(const Capabilities& caps, const Values& view, bool hasDevice, double lockoutRemaining);

Icon modeIcon(const std::string& mode);
std::string modeLabelFr(const std::string& mode);
// The word on the pill of a widget: "Chaud", "Sec"
std::string modeShortFr(const std::string& mode);
std::string fanLabelFr(const std::string& option);
std::string swingLabelFr(const std::string& option);
std::string faultLabelFr(const std::string& fault, bool shortForm);

// "2 min 20", "45 s": the full-screen view counts the seconds
std::string formatDuration(int seconds);
// "3 min": the widgets round up to the minute
std::string formatMinutes(int seconds);

// Room reading: one decimal, none when it is round ("21°", "25.4°")
std::string formatReading(const std::optional<double>& value);

// "22°", "22.5°": a round value drops its decimal
std::string formatShort(double value, double step);

// "22.5°", "21-23°" in a range, "--" while unknown, "" when the mode has no setpoint
std::string setpointText(SetpointKind kind, const Values& view, const Bounds& bounds);
// shortForm: "Écart min. 2°" for the narrow inside of a dial
std::string minRangeText(const Bounds& bounds, bool shortForm = false);

// Both handles held min_range apart: the inner buttons are refused
bool atMinRange(const Capabilities& caps, SetpointKind kind, const Values& view);

struct Steppers
{
    bool minus = false;
    bool plus = false;
    bool lowMinus = false;
    bool lowPlus = false;
    bool highMinus = false;
    bool highPlus = false;
};
Steppers steppers(const Controller& controller);

// A lock-out is told as seconds left at publication: the panel counts down by
// itself. The gauge needs the length, which the server does not publish, so it
// is the first value seen for the wait.
class LockoutClock
{
public:
    void observe(const std::optional<Lockout>& lockout, uint32_t nowMs);
    double remaining(uint32_t nowMs) const;
    double elapsedFraction(uint32_t nowMs) const;
    bool active() const { return active_; }

private:
    bool active_ = false;
    double base_ = 0;
    double total_ = 0;
    uint32_t at_ = 0;
};

// The dial is a 300 degree arc, open at the bottom: LVGL angles run clockwise
// from 3 o'clock, so it starts at 120 and ends at 60.
constexpr int kDialStart = 120;
constexpr int kDialSpan = 300;

double dialFraction(const Bounds& bounds, double value);
int dialAngle(double fraction);

struct Dial
{
    SetpointKind kind = SetpointKind::None;
    // Arc from the start of the dial to `to` (single), or between two handles (range)
    std::optional<double> to;
    std::optional<double> low;
    std::optional<double> high;
    std::optional<double> room;
    Tone fill = Tone::Muted;
    Tone knob = Tone::White;
    Tone lowKnob = Tone::Heat;
    Tone highKnob = Tone::Cool;
};
Dial dialFor(const Capabilities& caps, SetpointKind kind, const Values& view, const Status& status);

// Where the full-screen view puts things on a screen of w x h
struct DetailMetrics
{
    bool landscape = false;
    int margin = 24;
    int dial = 0;
    int stroke = 14;
    int knob = 16;
    int button = 80;
    int columnWidth = 0;        // landscape: the dial's column
    bool stacked = false;       // range: one -/+ pair above the other
    int pill = 64;              // mode pill and settings button height
    int numberPx = 80;          // single setpoint: 80 (degree lettered apart) or 60
};
DetailMetrics detailMetrics(int w, int h, SetpointKind kind);

// The climate inside a 2x2 or larger widget, content area w x h: mode pill and
// settings button on top, dial, "- 22 +" line, title at the bottom
DetailMetrics widgetMetrics(int w, int h, SetpointKind kind);

// Height the dial takes for a diameter: the arc is open at the bottom
int dialHeight(int diameter, int stroke, int knob);

} // namespace climate

#include "climate_present.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace climate
{

namespace
{

constexpr double kEpsilon = 1e-6;
// Keeps "2 min 20" and "Démarre dans 3 min" from breaking inside the figure
const char* const kNbsp = "\xC2\xA0";

bool isAction(const std::string& a)
{
    return a == "heating" || a == "cooling" || a == "drying" || a == "fan";
}

std::string stripDegree(const std::string& s)
{
    const std::string deg = "°";
    if (s.size() >= deg.size() && s.compare(s.size() - deg.size(), deg.size(), deg) == 0)
        return s.substr(0, s.size() - deg.size());
    return s;
}

} // namespace

Icon modeIcon(const std::string& mode)
{
    if (mode == "off") return Icon::Power;
    if (mode == "heat") return Icon::Flame;
    if (mode == "cool") return Icon::Snow;
    if (mode == "auto") return Icon::Auto;
    if (mode == "dry") return Icon::Drop;
    if (mode == "fan_only") return Icon::Fan;
    return Icon::Help;
}

std::string modeLabelFr(const std::string& mode)
{
    if (mode == "off") return "Arrêt";
    if (mode == "heat") return "Chauffage";
    if (mode == "cool") return "Froid";
    if (mode == "auto") return "Auto";
    if (mode == "dry") return "Déshumidification";
    if (mode == "fan_only") return "Ventilation";
    if (mode.empty()) return "Mode inconnu";
    return optionLabel(mode);
}

std::string modeShortFr(const std::string& mode)
{
    if (mode == "off") return "Arrêt";
    if (mode == "heat") return "Chaud";
    if (mode == "cool") return "Froid";
    if (mode == "auto") return "Auto";
    if (mode == "dry") return "Sec";
    if (mode == "fan_only") return "Ventil.";
    if (mode.empty()) return "Inconnu";
    return optionLabel(mode);
}

std::string fanLabelFr(const std::string& option)
{
    if (option == "auto") return "Auto";
    if (option == "quiet") return "Silencieux";
    if (option == "low") return "Faible";
    if (option == "medium") return "Moyen";
    if (option == "high") return "Fort";
    if (option == "on") return "Marche";
    if (option == "off") return "Arrêt";
    if (option == "middle") return "Moyen";
    if (option == "focus") return "Ciblé";
    if (option == "diffuse") return "Diffus";
    return optionLabel(option);
}

std::string swingLabelFr(const std::string& option)
{
    if (option == "off") return "Fixe";
    if (option == "vertical") return "Vertical";
    if (option == "horizontal") return "Horizontal";
    if (option == "both") return "Les deux";
    return optionLabel(option);
}

std::string faultLabelFr(const std::string& fault, bool shortForm)
{
    if (fault == "sensor_stale") return "Sonde muette";
    if (fault == "sensor_missing") return "Sonde absente";
    if (fault == "humidity_high") return shortForm ? "Hum. haute" : "Humidité trop haute";
    if (fault == "humidity_stale") return shortForm ? "Hum. muette" : "Sonde d'humidité muette";
    return optionLabel(fault);
}

std::string formatDuration(int seconds)
{
    seconds = std::max(0, seconds);
    const int m = seconds / 60;
    const int s = seconds % 60;
    char buf[48];
    if (m == 0)
        std::snprintf(buf, sizeof(buf), "%d%ss", s, kNbsp);
    else
        std::snprintf(buf, sizeof(buf), "%d%smin%s%02d", m, kNbsp, kNbsp, s);
    return buf;
}

std::string formatMinutes(int seconds)
{
    const int m = std::max(1, (std::max(0, seconds) + 59) / 60);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%smin", m, kNbsp);
    return buf;
}

std::string formatReading(const std::optional<double>& value)
{
    if (!value)
        return "--";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", *value);
    std::string s = buf;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0)
        s.resize(s.size() - 2);
    return s + "°";
}

Status statusOf(const Capabilities& caps, const Values& view, bool hasDevice, double lockoutRemaining)
{
    Status s;
    s.icon = modeIcon(view.mode);

    if (!hasDevice)
    {
        s.text = modeLabelFr("");
        s.shortText = "Inconnu";
        s.widgetText = s.text;
        return s;
    }

    const auto finish = [&s](Phase phase, Tone tone)
    {
        s.phase = phase;
        s.tone = tone;
        if (s.shortText.empty())
            s.shortText = s.text;
        if (s.widgetText.empty())
            s.widgetText = s.text;
    };

    if (view.connected && !*view.connected)
    {
        s.icon = Icon::Unlink;
        s.text = "Hors ligne";
        finish(Phase::Offline, Tone::Alert);
        return s;
    }

    s.canSet = !view.mode.empty();

    if (view.mode.empty())
    {
        s.text = "Mode inconnu";
        s.shortText = "Inconnu";
        finish(Phase::Unknown, Tone::Muted);
        return s;
    }

    if (view.mode == "off")
    {
        s.text = "Arrêté";
        finish(Phase::Off, Tone::Muted);
        return s;
    }

    if (!view.fault.empty())
    {
        s.text = faultLabelFr(view.fault, false);
        s.shortText = faultLabelFr(view.fault, true);
        finish(Phase::Fault, Tone::Wait);
        return s;
    }

    if (lockoutRemaining > 0)
    {
        const int seconds = static_cast<int>(std::ceil(lockoutRemaining - kEpsilon));
        s.icon = Icon::Clock;
        s.text = std::string("Démarre dans ") + formatDuration(seconds);
        s.widgetText = std::string("Démarre dans ") + formatMinutes(seconds);
        s.shortText = "Attente";
        finish(Phase::Waiting, Tone::Wait);
        return s;
    }

    if (isAction(view.action))
    {
        Tone tone = Tone::White;
        if (view.action == "heating")
        {
            s.text = "Chauffe";
            tone = Tone::Heat;
            s.backdrop = Backdrop::Heat;
        }
        else if (view.action == "cooling")
        {
            s.text = "Refroidit";
            tone = Tone::Cool;
            s.backdrop = Backdrop::Cool;
        }
        else if (view.action == "drying")
        {
            s.text = "Déshumidifie";
            s.shortText = "Sèche";
            tone = Tone::Cool;
            s.backdrop = Backdrop::Cool;
        }
        else
        {
            s.text = "Ventile";
            s.backdrop = Backdrop::Cool;
        }
        finish(Phase::Acting, tone);
        return s;
    }

    if (caps.reportsAction || !view.action.empty())
    {
        s.text = "En veille";
        s.shortText = "Veille";
        finish(Phase::Idle, Tone::Muted);
        return s;
    }

    // No action reported: the mode is all there is to say
    s.text = modeLabelFr(view.mode);
    finish(Phase::Mode, Tone::Muted);
    return s;
}

std::string formatShort(double value, double step)
{
    std::string s = stripDegree(formatSetpoint(value, step));
    if (s.find('.') != std::string::npos)
    {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    return s + "°";
}

std::string setpointText(SetpointKind kind, const Values& view, const Bounds& bounds)
{
    switch (kind)
    {
        case SetpointKind::Single:
            return view.target ? formatShort(*view.target, bounds.step) : "--";
        case SetpointKind::Range:
            if (!view.low || !view.high)
                return "--";
            // The en dash is not in the panel's fonts
            return stripDegree(formatShort(*view.low, bounds.step)) + "-" +
                   formatShort(*view.high, bounds.step);
        case SetpointKind::None:
            break;
    }
    return "";
}

std::string minRangeText(const Bounds& bounds, bool shortForm)
{
    return (shortForm ? "Écart min. " : "Écart minimum ") + formatShort(bounds.minRange, bounds.step);
}

bool atMinRange(const Capabilities& caps, SetpointKind kind, const Values& view)
{
    return kind == SetpointKind::Range && caps.bounds.minRange > 0 && view.low && view.high &&
           *view.high - *view.low <= caps.bounds.minRange + kEpsilon;
}

Steppers steppers(const Controller& controller)
{
    Steppers out;
    if (!controller.canEditSetpoint())
        return out;

    const Bounds& b = controller.caps().bounds;
    const Values v = controller.view();
    const auto moves = [](double before, double after) { return std::fabs(before - after) > kEpsilon; };

    if (controller.kind() == SetpointKind::Single && v.target)
    {
        out.minus = moves(*v.target, snap(b, *v.target - b.step));
        out.plus = moves(*v.target, snap(b, *v.target + b.step));
    }
    else if (controller.kind() == SetpointKind::Range && v.low && v.high)
    {
        const Range r{*v.low, *v.high};
        out.lowMinus = moves(r.low, moveLow(b, r, r.low - b.step).low);
        out.lowPlus = moves(r.low, moveLow(b, r, r.low + b.step).low);
        out.highMinus = moves(r.high, moveHigh(b, r, r.high - b.step).high);
        out.highPlus = moves(r.high, moveHigh(b, r, r.high + b.step).high);
    }
    return out;
}

void LockoutClock::observe(const std::optional<Lockout>& lockout, uint32_t nowMs)
{
    if (!lockout)
    {
        active_ = false;
        return;
    }

    const double r = lockout->remaining;
    // The server's count only goes down within one wait: a longer one is a new wait
    const bool running = active_ && remaining(nowMs) > 0;
    if (!running || r > remaining(nowMs) + 2.0)
        total_ = r;
    total_ = std::max(total_, r);

    base_ = r;
    at_ = nowMs;
    active_ = true;
}

double LockoutClock::remaining(uint32_t nowMs) const
{
    if (!active_)
        return 0;
    return std::max(0.0, base_ - static_cast<double>(nowMs - at_) / 1000.0);
}

double LockoutClock::elapsedFraction(uint32_t nowMs) const
{
    if (!active_ || total_ <= 0)
        return 0;
    return std::clamp(1.0 - remaining(nowMs) / total_, 0.0, 1.0);
}

double dialFraction(const Bounds& bounds, double value)
{
    if (bounds.max <= bounds.min)
        return 0;
    return std::clamp((value - bounds.min) / (bounds.max - bounds.min), 0.0, 1.0);
}

int dialAngle(double fraction)
{
    const int a = kDialStart + static_cast<int>(std::lround(kDialSpan * std::clamp(fraction, 0.0, 1.0)));
    return a % 360;
}

Dial dialFor(const Capabilities& caps, SetpointKind kind, const Values& view, const Status& status)
{
    Dial d;
    d.kind = kind;
    const Bounds& b = caps.bounds;

    if (view.current)
        d.room = dialFraction(b, *view.current);

    if (kind == SetpointKind::Single && view.target)
        d.to = dialFraction(b, *view.target);
    else if (kind == SetpointKind::Range && view.low && view.high)
    {
        d.low = dialFraction(b, *view.low);
        d.high = dialFraction(b, *view.high);
    }

    switch (status.phase)
    {
        case Phase::Offline:
            d.fill = Tone::Dim;
            d.knob = Tone::Muted;
            d.lowKnob = d.highKnob = Tone::Muted;
            break;
        case Phase::Off:
        case Phase::Idle:
        case Phase::Mode:
            d.fill = Tone::Muted;
            d.knob = Tone::White;
            break;
        default:
            d.fill = d.knob = status.tone;
            break;
    }

    // Between the handles the arc only lights while the device acts
    if (kind == SetpointKind::Range && status.phase != Phase::Acting && status.phase != Phase::Offline)
        d.fill = Tone::Dim;
    return d;
}

int dialHeight(int diameter, int stroke, int knob)
{
    const int pad = std::max(knob + 2, stroke / 2) + 1;
    const double r = diameter / 2.0 - pad;
    return static_cast<int>(std::ceil(diameter / 2.0 + r * std::cos(M_PI / 6) + pad));
}

DetailMetrics detailMetrics(int w, int h, SetpointKind kind)
{
    DetailMetrics m;
    m.landscape = w > h;

    const bool range = kind == SetpointKind::Range;
    const int pair = 80 + 8 + 110 + 8 + 80;
    const int inner = w - 2 * m.margin;
    const int availH = h - 2 * m.margin;

    constexpr int header = 64;
    constexpr int gap = 10;
    constexpr int pill = 10 + 64;
    constexpr int more = 14 + 56;
    // The line of "- 22° +", and under a range the "Écart minimum" note
    constexpr int note = gap + 8 + 26;

    int widest = 340;
    if (m.landscape)
    {
        m.columnWidth = std::min(400, inner / 2);
        m.stacked = range;
        widest = std::min({340, m.columnWidth, std::max(200, h * 7 / 12)});
    }
    else
    {
        m.stacked = range && 2 * pair + 24 > inner;
        widest = std::min(340, inner);
    }
    m.button = m.stacked ? 64 : 80;
    const int line = (m.stacked ? 2 * 64 + 4 : 80) + (range ? note : 0);

    // The largest dial that leaves room for the rest of its column
    // Portrait: header, dial, line, note, pill, spacer, "more" with a gap between each
    const int rest = m.landscape ? line + gap : header + line + pill + more + 6 * gap;
    m.dial = 160;
    for (int d = widest; d >= 160; d--)
    {
        const bool big = d >= 280;
        if (dialHeight(d, big ? 14 : 12, big ? 16 : 14) + rest <= availH)
        {
            m.dial = d;
            break;
        }
    }
    m.stroke = m.dial >= 280 ? 14 : 12;
    m.knob = m.dial >= 280 ? 16 : 14;
    return m;
}

DetailMetrics widgetMetrics(int w, int h, SetpointKind kind)
{
    DetailMetrics m;
    m.margin = 0;
    m.pill = 56;

    const bool range = kind == SetpointKind::Range;
    constexpr int gap = 6;
    constexpr int title = 30;
    const int widest = std::min(340, w);
    // Smallest dial worth drawing before the buttons give up some size
    const int worthwhile = range ? 210 : 240;

    for (int button : {80, 72, 64, 56})
    {
        const bool stacked = range && 2 * (button + 8 + 110 + 8 + button) + 24 > w;
        const int line = kind == SetpointKind::None ? 0 : (stacked ? 2 * button + 4 : button);
        // The 80 px number needs 190 px between the buttons
        const bool roomy = w - 2 * button - 16 >= 190;
        const int avail = h - (m.pill + title + line + 3 * gap);

        int dial = 0;
        for (int d = widest; d >= 100; d--)
        {
            const bool big = d >= 280;
            if (dialHeight(d, big ? 14 : 12, big ? 16 : 14) <= avail)
            {
                dial = d;
                break;
            }
        }

        const bool last = button == 56;
        if (last || (dial >= worthwhile && (range || kind == SetpointKind::None || roomy)))
        {
            m.button = button;
            m.stacked = stacked;
            m.dial = std::max(dial, 100);
            m.numberPx = roomy ? 80 : 60;
            break;
        }
    }
    m.stroke = m.dial >= 280 ? 14 : 12;
    m.knob = m.dial >= 280 ? 16 : 14;
    return m;
}

} // namespace climate

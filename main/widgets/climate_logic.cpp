#include "climate_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using nlohmann::json;

namespace climate
{

namespace
{

constexpr double kEpsilon = 1e-6;

bool same(double a, double b)
{
    return std::fabs(a - b) < kEpsilon;
}

bool same(const std::optional<double>& a, double b)
{
    return a && same(*a, b);
}

json parseObject(const std::string& text)
{
    json j = json::parse(text, nullptr, false);
    return j.is_object() ? j : json::object();
}

std::optional<double> number(const json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it == obj.end())
        return std::nullopt;

    double v;
    if (it->is_number())
        v = it->get<double>();
    else if (it->is_string())
    {
        const std::string& s = it->get_ref<const std::string&>();
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        if (s.empty() || *end != '\0')
            return std::nullopt;
    }
    else
        return std::nullopt;

    if (!std::isfinite(v))
        return std::nullopt;
    return v;
}

std::string text(const json& obj, const char* key)
{
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool flag(const json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it == obj.end())
        return false;
    if (it->is_boolean())
        return it->get<bool>();
    return it->is_string() && it->get_ref<const std::string&>() == "true";
}

std::vector<std::string> strings(const json& obj, const char* key)
{
    std::vector<std::string> out;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array())
        return out;
    for (const auto& v : *it)
        if (v.is_string() && !v.get_ref<const std::string&>().empty())
            out.push_back(v.get<std::string>());
    return out;
}

bool contains(const std::vector<std::string>& list, const std::string& v)
{
    return std::find(list.begin(), list.end(), v) != list.end();
}

// The epsilon keeps 0.1 + 0.2 style noise from tipping floor/ceil a step off
double onGrid(const Bounds& b, double value, double (*round)(double))
{
    const double steps = round((value - b.min) / b.step);
    const double v = std::clamp(b.min + steps * b.step, b.min, b.max);
    return std::round(v / kEpsilon) * kEpsilon;
}

double gridDown(double v) { return std::floor(v + 1e-9); }
double gridUp(double v) { return std::ceil(v - 1e-9); }
double gridNearest(double v) { return std::round(v); }

} // namespace

Layout chooseLayout(int cellsWide, int cellsHigh)
{
    if (cellsWide <= 1 && cellsHigh <= 1)
        return Layout::Compact;
    if (cellsHigh <= 1)
        return Layout::Wide;
    if (cellsWide <= 1)
        return Layout::Tall;
    return Layout::Large;
}

Capabilities Capabilities::parse(const std::string& capabilitiesJson, const std::string& iosJson)
{
    Capabilities c;
    const json caps = parseObject(capabilitiesJson);

    c.modes = strings(caps, "modes");
    c.range = flag(caps, "target_temp_range");

    auto t = caps.find("target_temp");
    if (t != caps.end() && t->is_object())
    {
        c.setpoint = true;
        c.bounds.min = number(*t, "min").value_or(c.bounds.min);
        c.bounds.max = number(*t, "max").value_or(c.bounds.max);
        c.bounds.step = number(*t, "step").value_or(c.bounds.step);
        c.bounds.minRange = number(*t, "min_range").value_or(0.0);
        if (!text(*t, "unit").empty())
            c.bounds.unit = text(*t, "unit");
    }
    else if (c.range)
        c.setpoint = true;

    if (!(c.bounds.step > 0))
        c.bounds.step = 0.5;
    if (c.bounds.max < c.bounds.min)
        c.bounds.max = c.bounds.min;
    if (c.bounds.minRange < 0)
        c.bounds.minRange = 0;

    // The server also publishes target_temp bounds for a range-only device; only
    // the target_temp link tells that a single setpoint exists.
    c.single = c.setpoint && (!c.range || !text(parseObject(iosJson), "target_temp").empty());

    c.fanModes = strings(caps, "fan_modes");
    c.swingModes = strings(caps, "swing_modes");
    c.reportsAction = flag(caps, "reports_action");
    return c;
}

bool Capabilities::hasMode(const std::string& mode) const
{
    return contains(modes, mode);
}

Values Values::parse(const std::string& stateJson, const std::string& scalarMode)
{
    Values v;
    const json state = parseObject(stateJson);

    v.mode = state.contains("mode") ? text(state, "mode") : scalarMode;
    v.current = number(state, "current_temp");
    v.target = number(state, "target_temp");
    v.low = number(state, "target_temp_low");
    v.high = number(state, "target_temp_high");
    v.action = text(state, "action");
    v.fanMode = text(state, "fan_mode");
    v.swingMode = text(state, "swing_mode");
    v.humidity = number(state, "humidity");
    v.fault = text(state, "fault");

    auto conn = state.find("connected");
    if (conn != state.end())
    {
        if (conn->is_boolean())
            v.connected = conn->get<bool>();
        else if (conn->is_string())
            v.connected = conn->get_ref<const std::string&>() != "false";
    }

    auto lock = state.find("lockout");
    if (lock != state.end() && lock->is_object())
    {
        const auto remaining = number(*lock, "remaining");
        if (remaining && *remaining > 0)
            v.lockout = Lockout{text(*lock, "output"), *remaining};
    }
    return v;
}

SetpointKind setpointKind(const Capabilities& caps, const std::string& mode)
{
    if (!caps.setpoint || mode == "fan_only")
        return SetpointKind::None;
    if (caps.range && (mode == "auto" || !caps.single))
        return SetpointKind::Range;
    return SetpointKind::Single;
}

double snap(const Bounds& b, double value)
{
    return onGrid(b, value, gridNearest);
}

Range moveLow(const Bounds& b, const Range& current, double low)
{
    // Clamping the ceiling to min would hand back a pair narrower than min_range
    if (current.high - b.minRange < b.min - kEpsilon)
        return current;
    const double ceiling = onGrid(b, current.high - b.minRange, gridDown);
    return {std::min(snap(b, low), ceiling), current.high};
}

Range moveHigh(const Bounds& b, const Range& current, double high)
{
    if (current.low + b.minRange > b.max + kEpsilon)
        return current;
    const double floor = onGrid(b, current.low + b.minRange, gridUp);
    return {current.low, std::max(snap(b, high), floor)};
}

std::string formatSetpoint(double value, double step)
{
    int decimals = 0;
    while (decimals < 3)
    {
        const double scaled = step * std::pow(10.0, decimals);
        if (std::fabs(scaled - std::round(scaled)) < kEpsilon)
            break;
        decimals++;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f°", decimals, value);
    return buf;
}

std::string formatTemperature(const std::optional<double>& value)
{
    if (!value)
        return "--";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f°", *value);
    return buf;
}

std::string modeLabel(const std::string& mode)
{
    if (mode.empty())
        return "--";
    if (mode == "fan_only")
        return "Fan";
    return optionLabel(mode);
}

std::string actionLabel(const std::string& action)
{
    if (action == "heating") return "Heating";
    if (action == "cooling") return "Cooling";
    if (action == "drying") return "Drying";
    if (action == "idle") return "Idle";
    if (action == "fan") return "Fan";
    if (action == "off") return "Off";
    return optionLabel(action);
}

std::string optionLabel(const std::string& option)
{
    std::string out = option;
    std::replace(out.begin(), out.end(), '_', ' ');
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

void Controller::update(const Capabilities& caps, const Values& server)
{
    caps_ = caps;
    server_ = server;

    // An echo settles the sent field; a pending one is still being edited
    if (mode_.sent && mode_.value == server.mode) mode_.clear();
    if (fan_.sent && fan_.value == server.fanMode) fan_.clear();
    if (swing_.sent && swing_.value == server.swingMode) swing_.clear();
    if (target_.sent && target_.value && same(server.target, *target_.value)) target_.clear();
    if (low_.sent && low_.value && same(server.low, *low_.value)) low_.clear();
    if (high_.sent && high_.value && same(server.high, *high_.value)) high_.clear();

    // What the device no longer offers cannot be sent
    if (mode_.value && !caps.hasMode(*mode_.value)) mode_.clear();
    if (fan_.value && !contains(caps.fanModes, *fan_.value)) fan_.clear();
    if (swing_.value && !contains(caps.swingModes, *swing_.value)) swing_.clear();
    if (!caps.single) target_.clear();
    if (!caps.range) { low_.clear(); high_.clear(); }
}

Values Controller::view() const
{
    Values v = server_;
    if (mode_.value) v.mode = *mode_.value;
    if (fan_.value) v.fanMode = *fan_.value;
    if (swing_.value) v.swingMode = *swing_.value;
    if (target_.value) v.target = target_.value;
    if (low_.value) v.low = low_.value;
    if (high_.value) v.high = high_.value;
    return v;
}

SetpointKind Controller::kind() const
{
    return setpointKind(caps_, view().mode);
}

bool Controller::isOff() const
{
    return view().mode == "off";
}

bool Controller::canEditSetpoint() const
{
    if (isOff())
        return false;

    const Values v = view();
    switch (kind())
    {
        case SetpointKind::Single: return v.target.has_value();
        // A half-known range would be refused whole by the server
        case SetpointKind::Range: return v.low.has_value() && v.high.has_value();
        case SetpointKind::None: break;
    }
    return false;
}

bool Controller::stepTarget(int steps)
{
    if (kind() != SetpointKind::Single || !canEditSetpoint())
        return false;

    const double current = *view().target;
    const double next = snap(caps_.bounds, current + steps * caps_.bounds.step);
    if (same(next, current))
        return false;
    target_.set(next);
    return true;
}

bool Controller::stepLow(int steps)
{
    if (kind() != SetpointKind::Range || !canEditSetpoint())
        return false;

    const Values v = view();
    const Range r = moveLow(caps_.bounds, {*v.low, *v.high}, *v.low + steps * caps_.bounds.step);
    if (same(r.low, *v.low))
        return false;
    low_.set(r.low);
    return true;
}

bool Controller::stepHigh(int steps)
{
    if (kind() != SetpointKind::Range || !canEditSetpoint())
        return false;

    const Values v = view();
    const Range r = moveHigh(caps_.bounds, {*v.low, *v.high}, *v.high + steps * caps_.bounds.step);
    if (same(r.high, *v.high))
        return false;
    high_.set(r.high);
    return true;
}

bool Controller::shiftRange(int steps)
{
    if (kind() != SetpointKind::Range || !canEditSetpoint() || steps == 0)
        return false;

    const Values v = view();
    const Bounds& b = caps_.bounds;
    const double low = *v.low + steps * b.step;
    // A pair the server sent too narrow leaves widened, not as it came
    const double high = std::max(*v.high, *v.low + b.minRange) + steps * b.step;
    if (low < b.min - kEpsilon || high > b.max + kEpsilon)
        return false;

    low_.set(snap(b, low));
    high_.set(snap(b, high));
    return true;
}

bool Controller::setMode(const std::string& mode)
{
    if (!caps_.hasMode(mode) || mode == view().mode)
        return false;

    // Back to the server's mode before anything left: nothing to send
    if (mode == server_.mode && !mode_.inFlight)
        mode_.clear();
    else
        mode_.set(mode);
    return true;
}

bool Controller::selectMode(const std::string& mode)
{
    return setMode(mode);
}

bool Controller::cycleMode()
{
    const auto& modes = caps_.modes;
    if (modes.empty())
        return false;

    auto it = std::find(modes.begin(), modes.end(), view().mode);
    if (it == modes.end() || ++it == modes.end())
        it = modes.begin();
    return setMode(*it);
}

bool Controller::selectFan(const std::string& fan)
{
    if (!contains(caps_.fanModes, fan) || fan == view().fanMode)
        return false;
    if (fan == server_.fanMode && !fan_.inFlight)
        fan_.clear();
    else
        fan_.set(fan);
    return true;
}

bool Controller::selectSwing(const std::string& swing)
{
    if (!contains(caps_.swingModes, swing) || swing == view().swingMode)
        return false;
    if (swing == server_.swingMode && !swing_.inFlight)
        swing_.clear();
    else
        swing_.set(swing);
    return true;
}

bool Controller::hasPending() const
{
    return mode_.pending() || target_.pending() || low_.pending() || high_.pending() ||
           fan_.pending() || swing_.pending();
}

bool Controller::hasSent() const
{
    return (mode_.sent && mode_.value) || (target_.sent && target_.value) ||
           (low_.sent && low_.value) || (high_.sent && high_.value) ||
           (fan_.sent && fan_.value) || (swing_.sent && swing_.value);
}

std::optional<Command> Controller::takeCommand()
{
    json partial = json::object();

    if (target_.pending())
    {
        if (same(server_.target, *target_.value) && !target_.inFlight)
            target_.clear();
        else
        {
            partial["target_temp"] = *target_.value;
            target_.markSent();
        }
    }

    if (low_.pending() || high_.pending())
    {
        // Sent as a pair: the server refuses a lone bound. The handle the user
        // did not touch takes the server's latest value.
        std::optional<double> low = low_.value ? low_.value : server_.low;
        std::optional<double> high = high_.value ? high_.value : server_.high;

        if (low && high)
        {
            if (low_.value && !high_.value)
                low = moveLow(caps_.bounds, {*low, *high}, *low).low;
            else if (high_.value && !low_.value)
                high = moveHigh(caps_.bounds, {*low, *high}, *high).high;

            if (same(server_.low, *low) && same(server_.high, *high) && !low_.inFlight && !high_.inFlight)
            {
                low_.clear();
                high_.clear();
            }
            else
            {
                partial["target_temp_low"] = *low;
                partial["target_temp_high"] = *high;
                if (low_.value) { low_.value = low; low_.markSent(); }
                if (high_.value) { high_.value = high; high_.markSent(); }
            }
        }
        else
        {
            low_.clear();
            high_.clear();
        }
    }

    if (fan_.pending())
    {
        partial["fan_mode"] = *fan_.value;
        fan_.markSent();
    }

    if (swing_.pending())
    {
        partial["swing_mode"] = *swing_.value;
        swing_.markSent();
    }

    Command cmd;
    if (mode_.pending())
    {
        mode_.markSent();
        if (partial.empty())
            cmd.mode = *mode_.value;
        else
            partial["mode"] = *mode_.value;
    }

    if (cmd.mode.empty() && partial.empty())
        return std::nullopt;

    cmd.partial = std::move(partial);
    return cmd;
}

void Controller::refused()
{
    mode_.clear();
    target_.clear();
    low_.clear();
    high_.clear();
    fan_.clear();
    swing_.clear();
}

void Controller::expireSent()
{
    // Unanswered: the server value is all there is to go by again
    auto expire = [](auto& f) { if (f.sent) f.clear(); else f.inFlight = false; };
    expire(mode_);
    expire(target_);
    expire(low_);
    expire(high_);
    expire(fan_);
    expire(swing_);
}

} // namespace climate

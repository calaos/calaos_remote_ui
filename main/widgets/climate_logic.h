#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// What the Climate widget shows and sends, kept free of LVGL so it runs in the
// host tests. Spec: CALAOS_DEVICES_FEAT.md §5.2, §5.3, §6, §9.3.
namespace climate
{

enum class SetpointKind { None, Single, Range };

// Cell footprint of the widget, not its pixel size: a 1x1 cell is 240x240 on the
// 86 panel and 213x266 on the 8" one, and both get the compact layout.
enum class Layout { Compact, Wide, Tall, Large };

Layout chooseLayout(int cellsWide, int cellsHigh);

struct Bounds
{
    double min = 7.0;
    double max = 35.0;
    double step = 0.5;
    double minRange = 0.0;
    std::string unit = "°C";
};

struct Capabilities
{
    std::vector<std::string> modes;
    bool setpoint = false;      // a target_temp capability: bounds for any setpoint
    bool range = false;         // target_temp_range
    bool single = false;        // a target_temp link, so a single setpoint exists
    Bounds bounds;
    std::vector<std::string> fanModes;
    std::vector<std::string> swingModes;
    bool reportsAction = false;

    // Malformed or missing documents yield an empty set, never an exception
    static Capabilities parse(const std::string& capabilitiesJson, const std::string& iosJson);

    bool hasMode(const std::string& mode) const;
};

// CLIM-6: a wanted output waiting for its lock-out, seconds left at publication
struct Lockout
{
    std::string output;
    double remaining = 0;
};

struct Values
{
    std::string mode;           // "" while unknown
    std::optional<double> current;
    std::optional<double> target;
    std::optional<double> low;
    std::optional<double> high;
    std::string action;
    std::string fanMode;
    std::string swingMode;

    // CLIM-6, absent until the server publishes them
    std::optional<double> humidity;
    std::optional<bool> connected;      // nullopt: nothing said, taken as connected
    std::string fault;                  // sensor_stale, humidity_high...
    std::optional<Lockout> lockout;

    // scalarMode is the IO state, which is the mode: used when the device state has none
    static Values parse(const std::string& stateJson, const std::string& scalarMode);
};

SetpointKind setpointKind(const Capabilities& caps, const std::string& mode);

double snap(const Bounds& b, double value);

struct Range
{
    double low;
    double high;
};

// One handle moves, held min_range away from the other: the server refuses a
// too-narrow pair whole, so the widget must never produce one.
Range moveLow(const Bounds& b, const Range& current, double low);
Range moveHigh(const Bounds& b, const Range& current, double high);

std::string formatSetpoint(double value, double step);
std::string optionLabel(const std::string& option);

// A command is a plain mode when only the mode changes, else a JSON partial update
struct Command
{
    std::string mode;
    nlohmann::json partial;

    bool isPlainMode() const { return !mode.empty(); }
};

/**
 * The server state with the user's edits laid over it.
 *
 * Gestures only edit a draft; takeCommand() turns what is pending into one
 * command, so a burst of taps leaves as a single set_state. A sent field stays
 * shown until the server echoes it, refuses it (refused()) or the widget gives
 * up waiting (expireSent()). A field the user did not touch always shows the
 * server value, including the other handle of a range.
 */
class Controller
{
public:
    void update(const Capabilities& caps, const Values& server);

    const Capabilities& caps() const { return caps_; }
    const Values& server() const { return server_; }
    Values view() const;

    SetpointKind kind() const;
    bool isOff() const;
    bool canEditSetpoint() const;

    bool stepTarget(int steps);
    bool stepLow(int steps);
    bool stepHigh(int steps);
    bool selectMode(const std::string& mode);
    bool selectFan(const std::string& fan);
    bool selectSwing(const std::string& swing);

    bool hasPending() const;
    bool hasSent() const;
    std::optional<Command> takeCommand();

    void refused();
    void expireSent();

private:
    template<typename T>
    struct Field
    {
        std::optional<T> value;
        bool sent = false;      // value is the last one sent
        // A command for this field is on its way: until the echo, the server
        // value is stale and an edit back to it must still be sent
        bool inFlight = false;

        bool pending() const { return value && !sent; }
        void set(const T& v) { value = v; sent = false; }
        void clear() { value.reset(); sent = false; inFlight = false; }
        void markSent() { sent = true; inFlight = true; }
    };

    bool setMode(const std::string& mode);

    Capabilities caps_;
    Values server_;

    Field<std::string> mode_;
    Field<double> target_;
    Field<double> low_;
    Field<double> high_;
    Field<std::string> fan_;
    Field<std::string> swing_;
};

} // namespace climate

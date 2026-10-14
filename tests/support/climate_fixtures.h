#pragma once

#include "headless_lvgl.h"
#include "ws_harness.h"
#include "widget_factory.h"
#include "widgets/climate_detail.h"
#include "widgets/climate_large_widget.h"
#include "widgets/climate_small_widget.h"
#include "misc/lv_area_private.h"
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Devices and helpers for the Climate widget tests. The fixtures follow the wire
// format of CALAOS_DEVICES_FEAT.md §5.3 and mirror the design mockups' data.
namespace climatefx
{

using nlohmann::json;

inline json item(const std::string& name, const std::string& impl, const std::string& mode,
                 const json& caps, json state)
{
    state["mode"] = mode;
    return {{"state", mode}, {"gui_type", "climate"}, {"name", name},
            {"device", {{"family", "climate"}, {"impl", impl}, {"capabilities", caps},
                        {"state", state}, {"ios", {{"mode", "m"}, {"target_temp", "t"}}}}}};
}

inline json fullCaps(bool fan = true, bool swing = true)
{
    json c = {{"modes", {"off", "heat", "cool", "auto", "dry", "fan_only"}},
              {"target_temp", {{"min", 16}, {"max", 30}, {"step", 0.5}, {"unit", "°C"}, {"min_range", 2}}},
              {"target_temp_range", true}, {"reports_action", true}};
    if (fan)
        c["fan_modes"] = {"auto", "quiet", "low", "medium", "high"};
    if (swing)
        c["swing_modes"] = {"off", "vertical", "horizontal", "both"};
    return c;
}

inline json thermostatCaps()
{
    return {{"modes", {"off", "heat"}}, {"target_temp", {{"min", 7}, {"max", 30}, {"step", 0.5}, {"unit", "°C"}}},
            {"reports_action", true}};
}

// Clim salon: cooling, a single setpoint, every secondary setting
inline json salon()
{
    return item("Clim salon", "proxy", "cool", fullCaps(),
                {{"current_temp", 25.4}, {"target_temp", 22.0}, {"target_temp_low", 20.0},
                 {"target_temp_high", 24.0}, {"action", "cooling"}, {"fan_mode", "auto"},
                 {"swing_mode", "off"}, {"humidity", 48}});
}

inline json chambre()
{
    json caps = {{"modes", {"off", "heat", "cool", "auto"}},
                 {"target_temp", {{"min", 16}, {"max", 30}, {"step", 0.5}, {"min_range", 2}}},
                 {"target_temp_range", true}, {"fan_modes", {"auto", "low", "high"}}, {"reports_action", true}};
    return item("PAC chambre", "proxy", "heat", caps,
                {{"current_temp", 18.6}, {"target_temp", 20.5}, {"action", "heating"}, {"fan_mode", "low"}});
}

// Auto at the minimum gap: the inner buttons are refused
inline json bureauAtStop()
{
    return item("Clim bureau", "proxy", "auto", fullCaps(true, true),
                {{"current_temp", 22.1}, {"target_temp", 22.0}, {"target_temp_low", 21.0},
                 {"target_temp_high", 23.0}, {"action", "idle"}, {"fan_mode", "auto"},
                 {"swing_mode", "vertical"}, {"humidity", 55}});
}

inline json sejour()
{
    return item("PAC séjour", "proxy", "auto", fullCaps(true, false),
                {{"current_temp", 18.9}, {"target_temp", 22.0}, {"target_temp_low", 19.5},
                 {"target_temp_high", 24.5}, {"action", "heating"}, {"fan_mode", "auto"}});
}

// A thermostat whose relay waits for its lock-out
inline json waiting(int remaining = 140)
{
    return item("Radiateur salle de bain", "thermostat", "heat", thermostatCaps(),
                {{"current_temp", 21.0}, {"target_temp", 22.5}, {"action", "idle"},
                 {"lockout", {{"output", "heat"}, {"remaining", remaining}}}});
}

inline json garageOff()
{
    return item("Chauffage garage", "thermostat", "off", thermostatCaps(),
                {{"current_temp", 12.3}, {"target_temp", 15.0}, {"action", "off"}});
}

inline json verandaOffline()
{
    return item("Clim véranda", "proxy", "heat", fullCaps(),
                {{"current_temp", 19.8}, {"target_temp", 21.0}, {"action", "heating"},
                 {"fan_mode", "auto"}, {"swing_mode", "off"}, {"connected", false}});
}

inline json unknownMode()
{
    json caps = fullCaps(true, false);
    json i = item("Clim cuisine", "proxy", "", caps, {{"current_temp", 23.0}, {"action", "idle"}});
    i["state"] = "";
    return i;
}

inline json cellier()
{
    json caps = {{"modes", {"off", "cool", "dry", "fan_only"}},
                 {"target_temp", {{"min", 16}, {"max", 30}, {"step", 0.5}}},
                 {"fan_modes", {"auto", "low", "high"}}, {"reports_action", true}};
    return item("Clim cellier", "proxy", "dry", caps,
                {{"current_temp", 20.2}, {"target_temp", 20.0}, {"action", "drying"},
                 {"fan_mode", "auto"}, {"humidity", 71}});
}

inline json withState(json it, const json& changes)
{
    for (auto e = changes.begin(); e != changes.end(); ++e)
    {
        if (e.value().is_null())
            it["device"]["state"].erase(e.key());
        else
            it["device"]["state"][e.key()] = e.value();
    }
    if (changes.contains("mode"))
        it["state"] = changes["mode"];
    return it;
}

inline void publish(const std::string& id, const json& it)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{id, it}});
}

struct Outbox
{
    std::vector<json> sent;
    CalaosWebSocketManager mgr;

    Outbox()
    {
        CalaosWebSocketManagerTestAccess::connect(mgr, [this](const std::string& m)
        {
            sent.push_back(json::parse(m));
            return true;
        });
        g_wsManager = &mgr;
    }
    ~Outbox() { g_wsManager = nullptr; }

    // What a command carries: a JSON object, or the plain mode string
    json command(size_t i) const
    {
        const auto& v = sent.at(i)["data"]["value"].get_ref<const std::string&>();
        return v.front() == '{' ? json::parse(v) : json(v);
    }
};

struct Cell
{
    int w;
    int h;
};

constexpr Cell kSquare{240, 240};   // 86 panel: 720x720 in 3x3
constexpr Cell kLandscape{200, 240};   // 7" panel: 800x480 in 4x2
constexpr Cell kTall{213, 266};     // 8" panel: 1280x800 in 6x3

inline std::unique_ptr<CalaosWidget> place(const std::string& id, int w, int h, Cell cell = kSquare, int x = 0,
                                           int y = 0)
{
    GridLayoutInfo grid;
    grid.gridWidth = headless::width() / cell.w;
    grid.gridHeight = headless::height() / cell.h;
    grid.screenWidth = headless::width();
    grid.screenHeight = headless::height();
    grid.cellWidth = cell.w;
    grid.cellHeight = cell.h;
    grid.padding = 8;

    CalaosProtocol::WidgetConfig config;
    config.type = "Climate";
    config.io_id = id;
    config.x = x;
    config.y = y;
    config.w = w;
    config.h = h;
    auto widget = WidgetFactory::getInstance().createWidget(headless::screen(), config, grid);
    lv_obj_update_layout(headless::screen());
    return widget;
}

inline bool shown(lv_obj_t* o)
{
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
            return false;
    return true;
}

inline void visit(lv_obj_t* root, const std::function<void(lv_obj_t*)>& fn)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
    {
        lv_obj_t* child = lv_obj_get_child(root, i);
        fn(child);
        visit(child, fn);
    }
}

inline std::vector<lv_obj_t*> labels(lv_obj_t* root, const std::string& text)
{
    std::vector<lv_obj_t*> out;
    visit(root, [&](lv_obj_t* o)
    {
        if (lv_obj_check_type(o, &lv_label_class) && shown(o) && text == lv_label_get_text(o))
            out.push_back(o);
    });
    return out;
}

inline bool showsText(lv_obj_t* root, const std::string& text)
{
    return !labels(root, text).empty();
}

// Every shown part lies inside the box, and parts side by side do not overlap
inline std::vector<std::string> overflows(lv_obj_t* root, const lv_area_t& box)
{
    std::vector<std::string> out;
    visit(root, [&](lv_obj_t* o)
    {
        if (!shown(o))
            return;
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        const std::string what = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "box";
        if (a.x1 < box.x1 - 1 || a.y1 < box.y1 - 1 || a.x2 > box.x2 + 1 || a.y2 > box.y2 + 1)
            out.push_back(what + " at " + std::to_string(a.x1) + "," + std::to_string(a.y1) + "-" +
                          std::to_string(a.x2) + "," + std::to_string(a.y2));

        lv_obj_t* parent = lv_obj_get_parent(o);
        const bool flex = lv_obj_get_style_layout(parent, LV_PART_MAIN) == LV_LAYOUT_FLEX;
        for (uint32_t i = 0; flex && i < lv_obj_get_child_count(parent); i++)
        {
            lv_obj_t* sib = lv_obj_get_child(parent, i);
            if (sib == o || !shown(sib) || lv_obj_get_index(sib) < lv_obj_get_index(o))
                continue;
            lv_area_t b, common;
            lv_obj_get_coords(sib, &b);
            if (lv_area_intersect(&common, &a, &b) && lv_area_get_width(&common) > 1 &&
                lv_area_get_height(&common) > 1)
                out.push_back(what + " overlaps a sibling");
        }
    });
    return out;
}

// Writes the whole display when CLIMATE_SHOTS_DIR is set
inline void shootScreen(const std::string& name)
{
    const char* dir = std::getenv("CLIMATE_SHOTS_DIR");
    if (!dir)
        return;
    headless::render();
    lv_area_t a = {0, 0, headless::width() - 1, headless::height() - 1};
    EXPECT_TRUE(headless::writePng(std::string(dir) + "/" + name + ".png", a));
}

} // namespace climatefx

struct ClimateSmallWidgetTestAccess
{
    static ClimateDetail* detail(CalaosWidget& w) { return static_cast<ClimateSmallWidget&>(w).detail_.get(); }
};

struct ClimateLargeWidgetTestAccess
{
    static ClimateDetail& panel(CalaosWidget& w) { return *static_cast<ClimateLargeWidget&>(w).panel_; }
};

struct ClimateDetailTestAccess
{
    using Op = ClimateDetail::Op;

    static size_t bindingCount(ClimateDetail& d) { return d.bindings_.size() + (d.popupBindings_ ? d.popupBindings_->size() : 0); }
    static lv_obj_t* overlay(ClimateDetail& d) { return d.overlay_; }
    static lv_obj_t* dialContent(ClimateDetail& d) { return d.dial_->content(); }
    static lv_obj_t* body(ClimateDetail& d) { return d.body_; }
    static lv_obj_t* titleLabel(ClimateDetail& d) { return d.title_label_; }
    static lv_obj_t* dial(ClimateDetail& d) { return d.dial_->get(); }
    static lv_obj_t* pill(ClimateDetail& d) { return d.pill_; }

    static std::vector<lv_obj_t*> controls(ClimateDetail& d, Op op, const std::string& value = "")
    {
        std::vector<lv_obj_t*> out;
        for (const auto* list : {&d.controls_, &d.popupControls_})
            for (const auto& c : *list)
                if (c.op == op && c.value == value && climatefx::shown(c.obj))
                    out.push_back(c.obj);
        return out;
    }

    static void click(ClimateDetail& d, Op op, const std::string& value = "", size_t index = 0)
    {
        auto found = controls(d, op, value);
        ASSERT_GT(found.size(), index) << "no visible control";
        lv_obj_send_event(found[index], LV_EVENT_CLICKED, nullptr);
    }

    static bool enabled(ClimateDetail& d, Op op, const std::string& value = "", size_t index = 0)
    {
        auto found = controls(d, op, value);
        return found.size() > index && !lv_obj_has_state(found[index], LV_STATE_DISABLED);
    }
};

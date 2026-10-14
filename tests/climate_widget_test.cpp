#include <gtest/gtest.h>
#include "support/headless_lvgl.h"
#include "support/ws_harness.h"
#include "widget_factory.h"
#include "widgets/climate_widget.h"
#include "misc/lv_area_private.h"
#include <cstdlib>
#include <functional>
#include <memory>

using nlohmann::json;

namespace
{

json fullHvac()
{
    return json::parse(R"({
        "state": "heat", "gui_type": "climate", "name": "Living room AC",
        "device": {"family": "climate", "impl": "proxy",
            "capabilities": {"modes": ["off", "heat", "cool", "auto", "dry", "fan_only"],
                "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C", "min_range": 2},
                "target_temp_range": true, "fan_modes": ["auto", "low", "high"],
                "swing_modes": ["off", "vertical"], "reports_action": true, "remote_temp": true},
            "state": {"mode": "heat", "current_temp": 23.4, "target_temp": 21.0,
                "target_temp_low": 19.0, "target_temp_high": 24.0, "action": "heating",
                "fan_mode": "auto", "swing_mode": "off"},
            "ios": {"mode": "ac_mode", "target_temp": "ac_target",
                "target_temp_low": "ac_low", "target_temp_high": "ac_high"}}})");
}

json heatOnly()
{
    return json::parse(R"({
        "state": "heat", "gui_type": "climate", "name": "Bathroom",
        "device": {"family": "climate", "impl": "thermostat",
            "capabilities": {"modes": ["off", "heat"],
                "target_temp": {"min": 7, "max": 35, "step": 0.5, "unit": "°C"}, "reports_action": true},
            "state": {"mode": "heat", "current_temp": 19.2, "target_temp": 20.5, "action": "heating"},
            "ios": {"target_temp": "sdb_target", "current_temp": "sdb_temp"}}})");
}

json rangeOnly()
{
    return json::parse(R"({
        "state": "cool", "gui_type": "climate", "name": "Office",
        "device": {"family": "climate", "impl": "thermostat",
            "capabilities": {"modes": ["off", "heat", "cool", "auto"],
                "target_temp": {"min": 10, "max": 30, "step": 0.5, "unit": "°C", "min_range": 1},
                "target_temp_range": true, "reports_action": true},
            "state": {"mode": "cool", "current_temp": 25.1, "target_temp_low": 19.0,
                "target_temp_high": 24.0, "action": "cooling"},
            "ios": {"target_temp_low": "off_low", "target_temp_high": "off_high"}}})");
}

json withMode(json item, const std::string& mode)
{
    item["state"] = mode;
    item["device"]["state"]["mode"] = mode;
    return item;
}

void publish(const std::string& id, const json& item)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{id, item}});
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
};

struct Cell
{
    int w;
    int h;
};

constexpr Cell kSquare{240, 240};   // 86 panel, 720x720 in 3x3
constexpr Cell kTall{213, 266};     // 8" panel, 1280x800 in 6x3

std::unique_ptr<CalaosWidget> place(const std::string& id, int w, int h, Cell cell = kSquare)
{
    GridLayoutInfo grid;
    grid.gridWidth = 6;
    grid.gridHeight = 3;
    grid.screenWidth = headless::kWidth;
    grid.screenHeight = headless::kHeight;
    grid.cellWidth = cell.w;
    grid.cellHeight = cell.h;
    grid.padding = 8;

    CalaosProtocol::WidgetConfig config;
    config.type = "Climate";
    config.io_id = id;
    config.x = 0;
    config.y = 0;
    config.w = w;
    config.h = h;
    auto widget = WidgetFactory::getInstance().createWidget(headless::screen(), config, grid);
    lv_obj_update_layout(headless::screen());
    return widget;
}

bool shown(lv_obj_t* o)
{
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
            return false;
    return true;
}

void visit(lv_obj_t* root, const std::function<void(lv_obj_t*)>& fn)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
    {
        lv_obj_t* child = lv_obj_get_child(root, i);
        fn(child);
        visit(child, fn);
    }
}

std::vector<lv_obj_t*> labels(CalaosWidget& w, const std::string& text)
{
    std::vector<lv_obj_t*> out;
    visit(w.get(), [&](lv_obj_t* o)
    {
        if (lv_obj_check_type(o, &lv_label_class) && shown(o) && text == lv_label_get_text(o))
            out.push_back(o);
    });
    return out;
}

} // namespace

struct ClimateWidgetTestAccess
{
    // The -/+ buttons are drawn, not lettered: "+" and "-" name them here
    static std::vector<lv_obj_t*> buttons(CalaosWidget& w, const std::string& sign)
    {
        using Op = ClimateWidget::Op;
        std::vector<lv_obj_t*> out;
        for (const auto& [op, obj] : static_cast<ClimateWidget&>(w).buttons_)
        {
            const bool plus = op == Op::TargetUp || op == Op::LowUp || op == Op::HighUp;
            if ((sign == "+") == plus && shown(obj))
                out.push_back(obj);
        }
        return out;
    }
};

namespace
{

std::vector<lv_obj_t*> targets(CalaosWidget& w, const std::string& text)
{
    if (text == "+" || text == "-")
        return ClimateWidgetTestAccess::buttons(w, text);

    std::vector<lv_obj_t*> out;
    for (lv_obj_t* l : labels(w, text))
        out.push_back(lv_obj_get_parent(l));
    return out;
}

bool showsText(CalaosWidget& w, const std::string& text)
{
    return !labels(w, text).empty();
}

void click(CalaosWidget& w, const std::string& text, size_t index = 0)
{
    auto found = targets(w, text);
    ASSERT_GT(found.size(), index) << "no visible \"" << text << "\"";
    lv_obj_send_event(found[index], LV_EVENT_CLICKED, nullptr);
}

bool disabled(CalaosWidget& w, const std::string& text)
{
    auto found = targets(w, text);
    return !found.empty() && lv_obj_has_state(found[0], LV_STATE_DISABLED);
}

json command(const json& message)
{
    const auto& v = message["data"]["value"].get_ref<const std::string&>();
    return v.front() == '{' ? json::parse(v) : json(v);
}

// Every shown part lies inside the widget's padding box
std::vector<std::string> overflows(CalaosWidget& w)
{
    lv_area_t box;
    lv_obj_get_content_coords(w.get(), &box);

    std::vector<std::string> out;
    visit(w.get(), [&](lv_obj_t* o)
    {
        if (!shown(o))
            return;
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        if (a.x1 < box.x1 - 1 || a.y1 < box.y1 - 1 || a.x2 > box.x2 + 1 || a.y2 > box.y2 + 1)
        {
            std::string what = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "box";
            out.push_back(what + " at " + std::to_string(a.x1) + "," + std::to_string(a.y1) + "-" +
                          std::to_string(a.x2) + "," + std::to_string(a.y2));
        }

        // Laid out side by side, siblings must not cover each other
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
                out.push_back(std::string(lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "box") +
                              " overlaps a sibling");
        }
    });
    return out;
}

void shoot(CalaosWidget& w, const std::string& name)
{
    const char* dir = std::getenv("CLIMATE_SHOTS_DIR");
    if (!dir)
        return;
    headless::render();
    lv_area_t a;
    lv_obj_get_coords(w.get(), &a);
    lv_area_increase(&a, 8, 8);
    EXPECT_TRUE(headless::writePng(std::string(dir) + "/" + name + ".png", a));
}

} // namespace

TEST(ClimateWidget, IsRegisteredForEverySize)
{
    for (auto [w, h] : {std::pair{1, 1}, {2, 1}, {1, 2}, {2, 2}, {3, 1}, {3, 3}})
        EXPECT_TRUE(WidgetFactory::getInstance().isRegistered("Climate", w, h)) << w << "x" << h;

    auto widget = place("cw_reg", 2, 2);
    EXPECT_NE(dynamic_cast<ClimateWidget*>(widget.get()), nullptr);
}

TEST(ClimateWidget, EveryFixtureFitsEveryCell)
{
    const struct { const char* name; json item; } fixtures[] = {
        {"full_hvac", fullHvac()},
        {"full_hvac_auto", withMode(fullHvac(), "auto")},
        {"full_hvac_fan_only", withMode(fullHvac(), "fan_only")},
        {"full_hvac_off", withMode(fullHvac(), "off")},
        {"heat_only", heatOnly()},
        {"range_only", rangeOnly()},
    };
    const struct { int w; int h; } sizes[] = {{2, 2}, {3, 2}, {3, 3}};
    const struct { const char* name; Cell cell; } cells[] = {{"240x240", kSquare}, {"213x266", kTall}};

    for (const auto& f : fixtures)
    {
        publish("cw_fit", f.item);
        for (const auto& c : cells)
            for (const auto& s : sizes)
            {
                auto widget = place("cw_fit", s.w, s.h, c.cell);
                const std::string name = std::string(f.name) + "_" + std::to_string(s.w) + "x" +
                                         std::to_string(s.h) + "_" + c.name;
                for (const auto& o : overflows(*widget))
                    ADD_FAILURE() << name << ": " << o;
                shoot(*widget, name);
            }
    }
}

TEST(ClimateWidget, TapsLeaveAsOneCommandAfterAPause)
{
    Outbox out;
    publish("cw_taps", fullHvac());
    auto w = place("cw_taps", 2, 2);

    click(*w, "+");
    click(*w, "+");
    click(*w, "+");
    EXPECT_TRUE(showsText(*w, "22.5"));
    headless::advance(ClimateWidget::kCoalesceMs - 100);
    EXPECT_TRUE(out.sent.empty());

    headless::advance(200);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.sent[0]["msg_id"], "ui_cmd:cw_taps");
    EXPECT_EQ(command(out.sent[0]), (json{{"target_temp", 22.5}}));
}

TEST(ClimateWidget, ARefusalShowsTheServerValueAtOnce)
{
    Outbox out;
    publish("cw_refused", fullHvac());
    auto w = place("cw_refused", 2, 2);

    click(*w, "+");
    headless::advance(ClimateWidget::kCoalesceMs + 50);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_TRUE(showsText(*w, "21.5"));

    receive(json{{"msg", "set_state"}, {"msg_id", "ui_cmd:cw_refused"}, {"data", {{"success", "false"}}}}.dump());
    EXPECT_TRUE(showsText(*w, "21.0"));
}

TEST(ClimateWidget, AnUnansweredCommandGivesWayToTheServer)
{
    Outbox out;
    publish("cw_echo", fullHvac());
    auto w = place("cw_echo", 2, 2);

    click(*w, "-");
    headless::advance(ClimateWidget::kCoalesceMs + 50);
    EXPECT_TRUE(showsText(*w, "20.5"));
    headless::advance(ClimateWidget::kEchoTimeoutMs + 50);
    EXPECT_TRUE(showsText(*w, "21.0"));
}

TEST(ClimateWidget, TheEchoSettlesTheDisplayedValue)
{
    Outbox out;
    publish("cw_echoed", fullHvac());
    auto w = place("cw_echoed", 2, 2);

    click(*w, "+");
    headless::advance(ClimateWidget::kCoalesceMs + 50);
    json echoed = fullHvac();
    echoed["device"]["state"]["target_temp"] = 21.5;
    publish("cw_echoed", echoed);

    headless::advance(ClimateWidget::kEchoTimeoutMs + 50);
    EXPECT_TRUE(showsText(*w, "21.5"));
}

TEST(ClimateWidget, ModeButtonsSendAtOnce)
{
    Outbox out;
    publish("cw_mode", fullHvac());
    auto w = place("cw_mode", 2, 2);

    click(*w, "Cool");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(command(out.sent[0]), json("cool"));
    EXPECT_EQ(out.sent[0]["msg_id"], "ui_cmd:cw_mode");

    click(*w, "Low");
    ASSERT_EQ(out.sent.size(), 2u);
    EXPECT_EQ(command(out.sent[1]), (json{{"fan_mode", "low"}}));
}

TEST(ClimateWidget, ThePendingSetpointLeavesWithTheMode)
{
    Outbox out;
    publish("cw_with_mode", fullHvac());
    auto w = place("cw_with_mode", 2, 2);

    click(*w, "+");
    click(*w, "Cool");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(command(out.sent[0]), (json{{"mode", "cool"}, {"target_temp", 21.5}}));
    headless::advance(ClimateWidget::kCoalesceMs + 50);
    EXPECT_EQ(out.sent.size(), 1u);
}

TEST(ClimateWidget, OffGreysAndFanOnlyHidesTheSetpoint)
{
    publish("cw_gate", withMode(fullHvac(), "off"));
    auto w = place("cw_gate", 2, 2);
    // The large layout raises the degree sign into a label of its own
    EXPECT_TRUE(showsText(*w, "21.0"));
    EXPECT_TRUE(disabled(*w, "+"));
    EXPECT_TRUE(disabled(*w, "-"));

    publish("cw_gate", withMode(fullHvac(), "fan_only"));
    EXPECT_FALSE(showsText(*w, "21.0"));
    EXPECT_TRUE(targets(*w, "+").empty());
    EXPECT_TRUE(showsText(*w, "23.4°"));

    publish("cw_gate", withMode(fullHvac(), "auto"));
    EXPECT_TRUE(showsText(*w, "19.0°"));
    EXPECT_TRUE(showsText(*w, "24.0°"));
    EXPECT_FALSE(disabled(*w, "+"));
}

TEST(ClimateWidget, ARangeOnlyDeviceSendsItsRangeAsAPair)
{
    Outbox out;
    publish("cw_range", rangeOnly());
    auto w = place("cw_range", 2, 2);
    EXPECT_TRUE(showsText(*w, "Heat to"));
    EXPECT_TRUE(showsText(*w, "Cool to"));

    click(*w, "+", 1);   // the high handle
    click(*w, "+", 1);
    headless::advance(ClimateWidget::kCoalesceMs + 50);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(command(out.sent[0]), (json{{"target_temp_low", 19.0}, {"target_temp_high", 25.0}}));
}

TEST(ClimateWidget, ADroppedDeviceStopsShowingItsState)
{
    publish("cw_dropped", fullHvac());
    auto w = place("cw_dropped", 2, 2);
    ASSERT_TRUE(showsText(*w, "21.0"));

    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "cw_other"}}})}});
    EXPECT_FALSE(showsText(*w, "21.0"));
    EXPECT_FALSE(showsText(*w, "Heating"));
}

TEST(ClimateWidget, ADeviceThatArrivesLaterIsBuilt)
{
    auto w = place("cw_late", 2, 2);
    EXPECT_FALSE(showsText(*w, "Heat"));

    publish("cw_late", fullHvac());
    EXPECT_TRUE(showsText(*w, "Heat"));
    EXPECT_TRUE(showsText(*w, "Vertical"));

    json fewer = fullHvac();
    fewer["device"]["capabilities"].erase("swing_modes");
    publish("cw_late", fewer);
    EXPECT_FALSE(showsText(*w, "Vertical"));
}

TEST(ClimateWidget, AnEditInProgressIsSentWhenThePageGoes)
{
    Outbox out;
    publish("cw_gone", heatOnly());
    auto w = place("cw_gone", 2, 2);
    click(*w, "+");
    w.reset();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(command(out.sent[0]), (json{{"target_temp", 21.0}}));
}

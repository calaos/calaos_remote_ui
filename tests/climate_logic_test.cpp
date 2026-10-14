#include <gtest/gtest.h>
#include "widgets/climate_logic.h"

using nlohmann::json;
using namespace climate;

namespace
{
// Spec 5.3: a full-HVAC proxy with a single setpoint and a range
const char* kProxyCaps = R"({
    "modes": ["off", "heat", "cool", "auto", "dry", "fan_only"],
    "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C", "min_range": 2},
    "target_temp_range": true,
    "fan_modes": ["auto", "low", "high"],
    "swing_modes": ["off", "vertical"],
    "reports_action": true,
    "remote_temp": true
})";
const char* kProxyIos = R"({"mode": "ac_mode", "target_temp": "ac_target",
                            "target_temp_low": "ac_low", "target_temp_high": "ac_high"})";

// Thermostat with output_cool but no single setpoint link: the range is all it has
const char* kRangeOnlyCaps = R"({
    "modes": ["off", "heat", "cool", "auto"],
    "target_temp": {"min": 10, "max": 30, "step": 0.5, "min_range": 1},
    "target_temp_range": true
})";
const char* kRangeOnlyIos = R"({"target_temp_low": "t_low", "target_temp_high": "t_high"})";

const char* kHeatOnlyCaps = R"({"modes": ["off", "heat"],
    "target_temp": {"min": 7, "max": 35, "step": 0.5}, "reports_action": true})";

Controller controller(const char* caps, const char* ios, const json& state, const std::string& scalar = "")
{
    Controller c;
    c.update(Capabilities::parse(caps, ios), Values::parse(state.dump(), scalar));
    return c;
}

Controller proxy(const std::string& mode = "heat")
{
    return controller(kProxyCaps, kProxyIos, {{"mode", mode}, {"current_temp", 23.4},
        {"target_temp", 21.0}, {"target_temp_low", 19.0}, {"target_temp_high", 24.0},
        {"fan_mode", "auto"}, {"swing_mode", "off"}, {"action", "heating"}});
}

Controller rangeOnly(const std::string& mode = "heat")
{
    return controller(kRangeOnlyCaps, kRangeOnlyIos, {{"mode", mode},
        {"target_temp_low", 19.0}, {"target_temp_high", 23.0}});
}

void serverSays(Controller& c, json state)
{
    c.update(c.caps(), Values::parse(state.dump(), ""));
}

json proxyState(double target, const std::string& mode = "heat")
{
    return {{"mode", mode}, {"target_temp", target}, {"target_temp_low", 19.0},
            {"target_temp_high", 24.0}, {"fan_mode", "auto"}, {"swing_mode", "off"}};
}
}

TEST(ClimateCapabilities, ReadsTheSpecObject)
{
    auto c = Capabilities::parse(kProxyCaps, kProxyIos);
    EXPECT_EQ(c.modes, (std::vector<std::string>{"off", "heat", "cool", "auto", "dry", "fan_only"}));
    EXPECT_TRUE(c.setpoint);
    EXPECT_TRUE(c.range);
    EXPECT_TRUE(c.single);
    EXPECT_DOUBLE_EQ(c.bounds.min, 16);
    EXPECT_DOUBLE_EQ(c.bounds.max, 30);
    EXPECT_DOUBLE_EQ(c.bounds.step, 0.5);
    EXPECT_DOUBLE_EQ(c.bounds.minRange, 2);
    EXPECT_EQ(c.fanModes, (std::vector<std::string>{"auto", "low", "high"}));
    EXPECT_EQ(c.swingModes, (std::vector<std::string>{"off", "vertical"}));
    EXPECT_TRUE(c.reportsAction);
}

TEST(ClimateCapabilities, ARangeWithoutATargetLinkIsRangeOnly)
{
    auto c = Capabilities::parse(kRangeOnlyCaps, kRangeOnlyIos);
    EXPECT_TRUE(c.range);
    EXPECT_FALSE(c.single);

    // Without a range, the target_temp capability implies its link
    EXPECT_TRUE(Capabilities::parse(kHeatOnlyCaps, "{}").single);
}

TEST(ClimateCapabilities, AbsentOrMalformedCapabilitiesAreEmpty)
{
    for (const char* bad : {"", "null", "[1]", "{\"modes\": 3", "\"x\""})
    {
        auto c = Capabilities::parse(bad, bad);
        EXPECT_TRUE(c.modes.empty()) << bad;
        EXPECT_FALSE(c.setpoint) << bad;
        EXPECT_FALSE(c.range) << bad;
    }

    auto c = Capabilities::parse(R"({"modes": ["heat", 4, null, ""], "fan_modes": "auto",
        "target_temp": {"min": "x", "max": 2, "step": 0, "min_range": -1}})", "{}");
    EXPECT_EQ(c.modes, (std::vector<std::string>{"heat"}));
    EXPECT_TRUE(c.fanModes.empty());
    EXPECT_DOUBLE_EQ(c.bounds.min, 7);
    EXPECT_DOUBLE_EQ(c.bounds.max, 7);
    EXPECT_DOUBLE_EQ(c.bounds.step, 0.5);
    EXPECT_DOUBLE_EQ(c.bounds.minRange, 0);
}

TEST(ClimateValues, ReadsTheStateAndFallsBackOnTheScalarMode)
{
    auto v = Values::parse(R"({"mode": "cool", "current_temp": 23.4, "target_temp": "21.5",
        "target_temp_low": true, "action": "cooling", "fan_mode": "auto"})", "heat");
    EXPECT_EQ(v.mode, "cool");
    EXPECT_DOUBLE_EQ(*v.current, 23.4);
    EXPECT_DOUBLE_EQ(*v.target, 21.5);
    EXPECT_FALSE(v.low.has_value());
    EXPECT_FALSE(v.high.has_value());
    EXPECT_EQ(v.action, "cooling");

    EXPECT_EQ(Values::parse(R"({"current_temp": 20})", "heat").mode, "heat");
    EXPECT_EQ(Values::parse("garbage", "off").mode, "off");
    EXPECT_FALSE(Values::parse(R"({"current_temp": "abc"})", "").current.has_value());
}

TEST(ClimateGating, ModeDecidesTheSetpoint)
{
    auto caps = Capabilities::parse(kProxyCaps, kProxyIos);
    EXPECT_EQ(setpointKind(caps, "heat"), SetpointKind::Single);
    EXPECT_EQ(setpointKind(caps, "cool"), SetpointKind::Single);
    EXPECT_EQ(setpointKind(caps, "off"), SetpointKind::Single);
    EXPECT_EQ(setpointKind(caps, "auto"), SetpointKind::Range);
    EXPECT_EQ(setpointKind(caps, "fan_only"), SetpointKind::None);
    EXPECT_EQ(setpointKind(caps, ""), SetpointKind::Single);

    EXPECT_EQ(setpointKind(Capabilities::parse(R"({"modes": ["off", "fan_only"]})", "{}"), "off"),
              SetpointKind::None);
}

TEST(ClimateGating, ARangeOnlyDeviceShowsItsRangeInEveryModeButFanOnly)
{
    auto caps = Capabilities::parse(kRangeOnlyCaps, kRangeOnlyIos);
    for (const char* mode : {"heat", "cool", "auto", "dry", "off", ""})
        EXPECT_EQ(setpointKind(caps, mode), SetpointKind::Range) << mode;
    EXPECT_EQ(setpointKind(caps, "fan_only"), SetpointKind::None);
}

TEST(ClimateGating, OffGreysTheSetpointAndAHalfKnownRangeIsNotEditable)
{
    EXPECT_TRUE(proxy("heat").canEditSetpoint());
    EXPECT_FALSE(proxy("off").canEditSetpoint());
    EXPECT_TRUE(proxy("off").isOff());
    EXPECT_FALSE(proxy("fan_only").canEditSetpoint());

    auto half = controller(kRangeOnlyCaps, kRangeOnlyIos, {{"mode", "auto"}, {"target_temp_low", 19}});
    EXPECT_FALSE(half.canEditSetpoint());
    EXPECT_FALSE(half.stepLow(1));

    auto unknown = controller(kHeatOnlyCaps, "{}", {{"mode", "heat"}});
    EXPECT_FALSE(unknown.canEditSetpoint());
    EXPECT_FALSE(unknown.stepTarget(1));
}

TEST(ClimateSetpoint, StepsAreSnappedAndBounded)
{
    Bounds b{16, 30, 0.5, 0, "°C"};
    EXPECT_DOUBLE_EQ(snap(b, 21.26), 21.5);
    EXPECT_DOUBLE_EQ(snap(b, 21.24), 21.0);
    EXPECT_DOUBLE_EQ(snap(b, 40), 30);
    EXPECT_DOUBLE_EQ(snap(b, -3), 16);

    auto c = controller(kProxyCaps, kProxyIos, proxyState(29.5));
    EXPECT_TRUE(c.stepTarget(1));
    EXPECT_DOUBLE_EQ(*c.view().target, 30);
    EXPECT_FALSE(c.stepTarget(1));
}

TEST(ClimateSetpoint, AHandleStaysMinRangeAwayFromTheOther)
{
    Bounds b{10, 30, 0.5, 2, "°C"};
    EXPECT_DOUBLE_EQ(moveLow(b, {19, 23}, 22.5).low, 21);
    EXPECT_DOUBLE_EQ(moveLow(b, {19, 23}, 20).low, 20);
    EXPECT_DOUBLE_EQ(moveHigh(b, {19, 23}, 19.5).high, 21);
    EXPECT_DOUBLE_EQ(moveHigh(b, {19, 23}, 31).high, 30);

    auto c = proxy("auto");
    EXPECT_TRUE(c.stepLow(3));
    EXPECT_DOUBLE_EQ(*c.view().low, 20.5);
    EXPECT_TRUE(c.stepLow(1));
    EXPECT_TRUE(c.stepLow(1));
    EXPECT_DOUBLE_EQ(*c.view().low, 21.5);
    EXPECT_TRUE(c.stepLow(1));
    EXPECT_DOUBLE_EQ(*c.view().low, 22);
    EXPECT_FALSE(c.stepLow(1));
    EXPECT_DOUBLE_EQ(*c.view().high, 24);
}

TEST(ClimateCommand, TapsCoalesceIntoOneCommand)
{
    auto c = proxy("heat");
    EXPECT_TRUE(c.stepTarget(1));
    EXPECT_TRUE(c.stepTarget(1));
    EXPECT_TRUE(c.stepTarget(1));
    EXPECT_TRUE(c.hasPending());

    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_FALSE(cmd->isPlainMode());
    EXPECT_EQ(cmd->partial, (json{{"target_temp", 22.5}}));

    EXPECT_FALSE(c.hasPending());
    EXPECT_FALSE(c.takeCommand().has_value());
}

TEST(ClimateCommand, AModeAloneIsSentPlain)
{
    auto c = proxy("heat");
    EXPECT_TRUE(c.selectMode("cool"));
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_TRUE(cmd->isPlainMode());
    EXPECT_EQ(cmd->mode, "cool");
}

TEST(ClimateCommand, AModeWithASetpointGoesTogetherAsJson)
{
    auto c = proxy("heat");
    c.stepTarget(-2);
    c.selectMode("cool");
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_FALSE(cmd->isPlainMode());
    EXPECT_EQ(cmd->partial, (json{{"mode", "cool"}, {"target_temp", 20.0}}));
}

TEST(ClimateCommand, TheActiveModeOrTheWayBackSendsNothing)
{
    auto c = proxy("heat");
    EXPECT_FALSE(c.selectMode("heat"));
    EXPECT_FALSE(c.selectMode("eco"));
    EXPECT_FALSE(c.takeCommand().has_value());

    EXPECT_TRUE(c.selectMode("cool"));
    EXPECT_EQ(c.view().mode, "cool");
    EXPECT_TRUE(c.selectMode("heat"));
    EXPECT_FALSE(c.takeCommand().has_value());

    c.stepTarget(1);
    c.stepTarget(-1);
    EXPECT_FALSE(c.takeCommand().has_value());
}

TEST(ClimateCommand, ARangeIsAlwaysSentAsAPair)
{
    auto c = proxy("auto");
    c.stepHigh(1);
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp_low", 19.0}, {"target_temp_high", 24.5}}));
}

TEST(ClimateCommand, TheUntouchedHandleFollowsTheServer)
{
    auto c = proxy("auto");
    c.stepLow(1);

    // Another client moves the high handle while this one is still tapping
    auto moved = proxyState(21, "auto");
    moved["target_temp_high"] = 22.0;
    serverSays(c, moved);
    EXPECT_DOUBLE_EQ(*c.view().high, 22);
    EXPECT_DOUBLE_EQ(*c.view().low, 19.5);

    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp_low", 19.5}, {"target_temp_high", 22.0}}));
}

TEST(ClimateCommand, TheTouchedHandleYieldsWhenTheServerNarrowsTheGap)
{
    auto c = proxy("auto");
    c.stepLow(4);   // 21
    auto moved = proxyState(21, "auto");
    moved["target_temp_high"] = 22.0;
    serverSays(c, moved);

    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp_low", 20.0}, {"target_temp_high", 22.0}}));
}

TEST(ClimateCommand, ARangeOnlyDeviceNeverSendsTargetTemp)
{
    for (const char* mode : {"heat", "cool", "auto", "off"})
    {
        // Even with a stray target_temp value in its state
        auto c = controller(kRangeOnlyCaps, kRangeOnlyIos, {{"mode", mode}, {"target_temp", 21.0},
            {"target_temp_low", 19.0}, {"target_temp_high", 23.0}});
        EXPECT_FALSE(c.stepTarget(1)) << mode;
        c.stepLow(-1);
        c.stepHigh(1);
        auto cmd = c.takeCommand();
        if (cmd)
            EXPECT_FALSE(cmd->partial.contains("target_temp")) << mode;
    }

    auto c = rangeOnly("heat");
    EXPECT_TRUE(c.stepLow(1));
    EXPECT_TRUE(c.stepHigh(1));
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp_low", 19.5}, {"target_temp_high", 23.5}}));
}

TEST(ClimateCommand, FanAndSwingAreChosenFromTheirLists)
{
    auto c = proxy("cool");
    EXPECT_FALSE(c.selectFan("turbo"));
    EXPECT_FALSE(c.selectFan("auto"));
    EXPECT_TRUE(c.selectFan("low"));
    EXPECT_TRUE(c.selectSwing("vertical"));
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"fan_mode", "low"}, {"swing_mode", "vertical"}}));

    auto back = proxy("cool");
    back.selectFan("low");
    EXPECT_TRUE(back.selectFan("auto"));
    EXPECT_FALSE(back.takeCommand().has_value());
}

TEST(ClimateDraft, ASentValueShowsUntilTheServerEchoesIt)
{
    auto c = proxy("heat");
    c.stepTarget(2);
    c.takeCommand();
    EXPECT_TRUE(c.hasSent());

    serverSays(c, proxyState(21));
    EXPECT_DOUBLE_EQ(*c.view().target, 22);

    serverSays(c, proxyState(22));
    EXPECT_FALSE(c.hasSent());

    // Settled: the next change from elsewhere shows at once
    serverSays(c, proxyState(18));
    EXPECT_DOUBLE_EQ(*c.view().target, 18);
}

TEST(ClimateDraft, APendingEditIsNotOverwrittenByTheServer)
{
    auto c = proxy("heat");
    c.stepTarget(1);
    serverSays(c, proxyState(25));
    EXPECT_DOUBLE_EQ(*c.view().target, 21.5);
}

TEST(ClimateDraft, ARefusalRevertsAtOnce)
{
    auto c = proxy("heat");
    c.selectMode("cool");
    c.stepTarget(1);
    c.takeCommand();
    c.stepTarget(1);

    c.refused();
    EXPECT_EQ(c.view().mode, "heat");
    EXPECT_DOUBLE_EQ(*c.view().target, 21);
    EXPECT_FALSE(c.hasPending());
    EXPECT_FALSE(c.hasSent());
}

TEST(ClimateDraft, AnUnansweredCommandExpiresButAPendingEditStays)
{
    auto c = proxy("heat");
    c.selectMode("cool");
    c.takeCommand();
    c.stepTarget(1);

    c.expireSent();
    EXPECT_EQ(c.view().mode, "heat");
    EXPECT_DOUBLE_EQ(*c.view().target, 21.5);
    EXPECT_TRUE(c.hasPending());
}

TEST(ClimateDraft, AnEditTheDeviceNoLongerOffersIsDropped)
{
    auto c = proxy("heat");
    c.selectFan("high");
    c.selectMode("dry");
    c.stepTarget(1);

    c.update(Capabilities::parse(R"({"modes": ["off", "heat"]})", "{}"), c.server());
    EXPECT_FALSE(c.takeCommand().has_value());
}

TEST(ClimateFormat, LabelsAndNumbers)
{
    EXPECT_EQ(formatSetpoint(21, 0.5), "21.0°");
    EXPECT_EQ(formatSetpoint(21, 1), "21°");
    EXPECT_EQ(formatSetpoint(21.25, 0.25), "21.25°");
    EXPECT_EQ(optionLabel("auto"), "Auto");
    EXPECT_EQ(optionLabel("heat_cool"), "Heat cool");
}

TEST(ClimateLayout, CellFootprintPicksTheLayout)
{
    EXPECT_EQ(chooseLayout(1, 1), Layout::Compact);
    EXPECT_EQ(chooseLayout(2, 1), Layout::Wide);
    EXPECT_EQ(chooseLayout(4, 1), Layout::Wide);
    EXPECT_EQ(chooseLayout(1, 2), Layout::Tall);
    EXPECT_EQ(chooseLayout(2, 2), Layout::Large);
    EXPECT_EQ(chooseLayout(3, 3), Layout::Large);
}

// A value sent and not yet echoed: going back to what the server still shows
// must be sent too, or the device ends on the first value.
TEST(ClimateDraft, StepBackToServerWhileInFlight)
{
    auto c = proxy("heat");
    c.stepTarget(1);
    ASSERT_EQ(c.takeCommand()->partial, (json{{"target_temp", 21.5}}));
    c.stepTarget(-1);
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp", 21.0}}));
}

TEST(ClimateDraft, StepBackToServerWhileInFlightRange)
{
    auto c = proxy("auto");
    c.stepHigh(1);
    ASSERT_TRUE(c.takeCommand().has_value());
    c.stepHigh(-1);
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp_low", 19.0}, {"target_temp_high", 24.0}}));
}

TEST(ClimateDraft, ModeBackToServerWhileInFlight)
{
    auto c = proxy("heat");
    c.selectMode("cool");
    ASSERT_TRUE(c.takeCommand().has_value());
    c.selectMode("dry");
    c.selectMode("heat");
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->mode, "heat");
}

TEST(ClimateDraft, AnEchoOfTheFirstValueDoesNotCancelTheSecond)
{
    auto c = proxy("heat");
    c.stepTarget(1);
    c.takeCommand();
    c.stepTarget(-1);
    serverSays(c, proxyState(21.5));
    EXPECT_DOUBLE_EQ(*c.view().target, 21);
    auto cmd = c.takeCommand();
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->partial, (json{{"target_temp", 21.0}}));
}

TEST(ClimateSetpoint, NearTheBoundsAHandleNeverBreaksMinRange)
{
    Bounds b{10, 30, 0.5, 2, "°C"};
    // A pair already narrower than min_range against a bound: no legal move,
    // the handle stays rather than jump to the bound
    EXPECT_DOUBLE_EQ(moveLow(b, {10.5, 11.5}, 11).low, 10.5);
    EXPECT_DOUBLE_EQ(moveHigh(b, {28.5, 29.5}, 29).high, 29.5);
}


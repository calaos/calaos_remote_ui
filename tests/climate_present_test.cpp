#include <gtest/gtest.h>
#include "widgets/climate_present.h"

using nlohmann::json;
using namespace climate;

namespace
{

const char* kCaps = R"({
    "modes": ["off", "heat", "cool", "auto", "dry", "fan_only"],
    "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C", "min_range": 2},
    "target_temp_range": true, "fan_modes": ["auto", "low"], "reports_action": true})";
const char* kIos = R"({"mode": "m", "target_temp": "t"})";
const char* kNoActionCaps = R"({"modes": ["off", "heat"], "target_temp": {"min": 7, "max": 30, "step": 0.5}})";

Values values(const json& state)
{
    return Values::parse(state.dump(), "");
}

Status status(const json& state, double lockout = 0, const char* caps = kCaps)
{
    return statusOf(Capabilities::parse(caps, kIos), values(state), true, lockout);
}

Controller controller(const json& state)
{
    Controller c;
    c.update(Capabilities::parse(kCaps, kIos), values(state));
    return c;
}

} // namespace

TEST(ClimatePresentStatus, TheActionDecidesTheWordAndItsColour)
{
    const struct { const char* action; const char* text; Tone tone; Backdrop back; } rows[] = {
        {"heating", "Chauffe", Tone::Heat, Backdrop::Heat},
        {"cooling", "Refroidit", Tone::Cool, Backdrop::Cool},
        {"drying", "Déshumidifie", Tone::Cool, Backdrop::Cool},
        {"fan", "Ventile", Tone::White, Backdrop::Cool},
        {"idle", "En veille", Tone::Muted, Backdrop::Off},
    };
    for (const auto& r : rows)
    {
        const Status s = status({{"mode", "auto"}, {"action", r.action}});
        EXPECT_EQ(s.text, r.text) << r.action;
        EXPECT_EQ(s.tone, r.tone) << r.action;
        EXPECT_EQ(s.backdrop, r.back) << r.action;
    }
    EXPECT_EQ(status({{"mode", "heat"}, {"action", "drying"}}).shortText, "Sèche");
    EXPECT_EQ(status({{"mode", "heat"}, {"action", "idle"}}).shortText, "Veille");
}

TEST(ClimatePresentStatus, OnlyAnActingDeviceLightsTheWidget)
{
    EXPECT_EQ(status({{"mode", "heat"}, {"action", "idle"}}).backdrop, Backdrop::Off);
    EXPECT_EQ(status({{"mode", "off"}, {"action", "heating"}}).backdrop, Backdrop::Off);
    EXPECT_EQ(status({{"mode", "heat"}, {"action", "heating"}, {"connected", false}}).backdrop, Backdrop::Off);
}

TEST(ClimatePresentStatus, OffUnknownAndOfflineAreToldAsSuch)
{
    const Status off = status({{"mode", "off"}, {"action", "off"}});
    EXPECT_EQ(off.text, "Arrêté");
    EXPECT_EQ(off.phase, Phase::Off);
    EXPECT_EQ(off.tone, Tone::Muted);
    EXPECT_EQ(off.icon, Icon::Power);

    const Status unknown = status({{"action", "idle"}});
    EXPECT_EQ(unknown.text, "Mode inconnu");
    EXPECT_EQ(unknown.shortText, "Inconnu");
    EXPECT_EQ(unknown.icon, Icon::Help);
    EXPECT_FALSE(unknown.canSet);

    const Status offline = status({{"mode", "heat"}, {"action", "heating"}, {"connected", false}});
    EXPECT_EQ(offline.text, "Hors ligne");
    EXPECT_EQ(offline.tone, Tone::Alert);
    EXPECT_EQ(offline.icon, Icon::Unlink);
    EXPECT_FALSE(offline.canSet);
}

TEST(ClimatePresentStatus, OfflineWinsOverEverythingElse)
{
    const Status s = status({{"mode", "off"}, {"connected", false}, {"fault", "sensor_stale"}}, 90);
    EXPECT_EQ(s.phase, Phase::Offline);
}

TEST(ClimatePresentStatus, ConnectedIsTakenAsTrueUntilTheServerSaysOtherwise)
{
    EXPECT_NE(status({{"mode", "heat"}, {"action", "idle"}}).phase, Phase::Offline);
    EXPECT_NE(status({{"mode", "heat"}, {"connected", true}}).phase, Phase::Offline);
    EXPECT_EQ(status({{"mode", "heat"}, {"connected", "false"}}).phase, Phase::Offline);
}

TEST(ClimatePresentStatus, ALockoutCountsTheWaitDown)
{
    const Status s = status({{"mode", "heat"}, {"action", "idle"}}, 140);
    EXPECT_EQ(s.phase, Phase::Waiting);
    EXPECT_EQ(s.tone, Tone::Wait);
    EXPECT_EQ(s.icon, Icon::Clock);
    EXPECT_EQ(s.text, "Démarre dans 2\xC2\xA0min\xC2\xA0" "20");
    EXPECT_EQ(s.widgetText, "Démarre dans 3\xC2\xA0min");
    EXPECT_EQ(s.shortText, "Attente");
    // The wait is not an action: the widget stays dark
    EXPECT_EQ(s.backdrop, Backdrop::Off);
}

TEST(ClimatePresentStatus, ADurationReadsInMinutesAndSeconds)
{
    EXPECT_EQ(formatDuration(45), "45\xC2\xA0s");
    EXPECT_EQ(formatDuration(60), "1\xC2\xA0min\xC2\xA0" "00");
    EXPECT_EQ(formatDuration(125), "2\xC2\xA0min\xC2\xA0" "05");
    EXPECT_EQ(formatMinutes(1), "1\xC2\xA0min");
    EXPECT_EQ(formatMinutes(180), "3\xC2\xA0min");
    EXPECT_EQ(formatMinutes(181), "4\xC2\xA0min");
}

TEST(ClimatePresentStatus, AFaultHasItsOwnWords)
{
    const struct { const char* fault; const char* text; const char* shortText; } rows[] = {
        {"sensor_stale", "Sonde muette", "Sonde muette"},
        {"sensor_missing", "Sonde absente", "Sonde absente"},
        {"humidity_high", "Humidité trop haute", "Hum. haute"},
    };
    for (const auto& r : rows)
    {
        const Status s = status({{"mode", "cool"}, {"action", "cooling"}, {"fault", r.fault}});
        EXPECT_EQ(s.phase, Phase::Fault) << r.fault;
        EXPECT_EQ(s.text, r.text) << r.fault;
        EXPECT_EQ(s.shortText, r.shortText) << r.fault;
        EXPECT_EQ(s.tone, Tone::Wait) << r.fault;
    }
    // A fault the panel does not know is still shown
    EXPECT_EQ(status({{"mode", "cool"}, {"fault", "pump_stuck"}}).text, "Pump stuck");
}

TEST(ClimatePresentStatus, AFaultOutranksTheWaitButNotOff)
{
    EXPECT_EQ(status({{"mode", "heat"}, {"fault", "sensor_stale"}}, 60).phase, Phase::Fault);
    EXPECT_EQ(status({{"mode", "off"}, {"fault", "sensor_stale"}}, 60).phase, Phase::Off);
}

TEST(ClimatePresentStatus, WithoutAnActionTheModeIsWhatIsSaid)
{
    const Status s = status({{"mode", "heat"}}, 0, kNoActionCaps);
    EXPECT_EQ(s.phase, Phase::Mode);
    EXPECT_EQ(s.text, "Chauffage");
    EXPECT_EQ(s.backdrop, Backdrop::Off);
}

TEST(ClimatePresentStatus, ADeviceThatIsGoneIsUnknown)
{
    const Status s = statusOf(Capabilities{}, Values{}, false, 0);
    EXPECT_EQ(s.text, "Mode inconnu");
    EXPECT_FALSE(s.canSet);
}

TEST(ClimatePresentLabels, ModesAndOptionsAreInFrench)
{
    EXPECT_EQ(modeLabelFr("heat"), "Chauffage");
    EXPECT_EQ(modeLabelFr("cool"), "Froid");
    EXPECT_EQ(modeLabelFr("dry"), "Déshumidification");
    EXPECT_EQ(modeLabelFr("fan_only"), "Ventilation");
    EXPECT_EQ(modeLabelFr("off"), "Arrêt");
    EXPECT_EQ(modeLabelFr(""), "Mode inconnu");
    EXPECT_EQ(fanLabelFr("quiet"), "Silencieux");
    EXPECT_EQ(fanLabelFr("medium"), "Moyen");
    EXPECT_EQ(swingLabelFr("off"), "Fixe");
    EXPECT_EQ(swingLabelFr("both"), "Les deux");
    // What is not translated still reads
    EXPECT_EQ(fanLabelFr("turbo_boost"), "Turbo boost");
}

TEST(ClimatePresentLabels, EveryModeHasItsIcon)
{
    EXPECT_EQ(modeIcon("off"), Icon::Power);
    EXPECT_EQ(modeIcon("heat"), Icon::Flame);
    EXPECT_EQ(modeIcon("cool"), Icon::Snow);
    EXPECT_EQ(modeIcon("auto"), Icon::Auto);
    EXPECT_EQ(modeIcon("dry"), Icon::Drop);
    EXPECT_EQ(modeIcon("fan_only"), Icon::Fan);
    EXPECT_EQ(modeIcon(""), Icon::Help);
}

TEST(ClimatePresentSetpoint, ARoundValueDropsItsDecimal)
{
    EXPECT_EQ(formatShort(22.0, 0.5), "22°");
    EXPECT_EQ(formatShort(22.5, 0.5), "22.5°");
    EXPECT_EQ(formatShort(7.0, 1.0), "7°");
    EXPECT_EQ(formatShort(20.0, 0.1), "20°");
    EXPECT_EQ(formatReading(21.0), "21°");
    EXPECT_EQ(formatReading(25.44), "25.4°");
    EXPECT_EQ(formatReading(std::nullopt), "--");
}

TEST(ClimatePresentSetpoint, TheTextFollowsTheKindOfSetpoint)
{
    const Bounds b{16, 30, 0.5, 2, "°C"};
    const Values v = values({{"mode", "auto"}, {"target_temp", 22.5}, {"target_temp_low", 21.0},
                             {"target_temp_high", 23.5}});
    EXPECT_EQ(setpointText(SetpointKind::Single, v, b), "22.5°");
    EXPECT_EQ(setpointText(SetpointKind::Range, v, b), "21-23.5°");
    EXPECT_EQ(setpointText(SetpointKind::None, v, b), "");
    EXPECT_EQ(setpointText(SetpointKind::Single, Values{}, b), "--");
    EXPECT_EQ(setpointText(SetpointKind::Range, values({{"target_temp_low", 21.0}}), b), "--");
}

TEST(ClimatePresentSetpoint, TheMinimumGapIsReachedWhenTheHandlesAreMinRangeApart)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    EXPECT_TRUE(atMinRange(caps, SetpointKind::Range,
                           values({{"target_temp_low", 21.0}, {"target_temp_high", 23.0}})));
    EXPECT_FALSE(atMinRange(caps, SetpointKind::Range,
                            values({{"target_temp_low", 21.0}, {"target_temp_high", 23.5}})));
    EXPECT_FALSE(atMinRange(caps, SetpointKind::Single,
                            values({{"target_temp_low", 21.0}, {"target_temp_high", 23.0}})));
    EXPECT_EQ(minRangeText(caps.bounds), "Écart minimum 2°");
}

TEST(ClimatePresentSteppers, ASingleSetpointStopsAtItsBounds)
{
    EXPECT_TRUE(steppers(controller({{"mode", "heat"}, {"target_temp", 22.0}})).plus);
    EXPECT_TRUE(steppers(controller({{"mode", "heat"}, {"target_temp", 22.0}})).minus);
    EXPECT_FALSE(steppers(controller({{"mode", "heat"}, {"target_temp", 30.0}})).plus);
    EXPECT_FALSE(steppers(controller({{"mode", "heat"}, {"target_temp", 16.0}})).minus);
}

TEST(ClimatePresentSteppers, TheInnerButtonsAreRefusedAtTheMinimumGap)
{
    const Steppers s = steppers(controller({{"mode", "auto"}, {"target_temp_low", 21.0},
                                            {"target_temp_high", 23.0}}));
    EXPECT_TRUE(s.lowMinus);
    EXPECT_FALSE(s.lowPlus);
    EXPECT_FALSE(s.highMinus);
    EXPECT_TRUE(s.highPlus);

    const Steppers apart = steppers(controller({{"mode", "auto"}, {"target_temp_low", 20.0},
                                                {"target_temp_high", 24.0}}));
    EXPECT_TRUE(apart.lowMinus && apart.lowPlus && apart.highMinus && apart.highPlus);
}

TEST(ClimatePresentSteppers, NothingMovesWhenTheDeviceIsOffOrHasNoValue)
{
    const Steppers off = steppers(controller({{"mode", "off"}, {"target_temp", 22.0}}));
    EXPECT_FALSE(off.minus || off.plus);
    const Steppers none = steppers(controller({{"mode", "heat"}}));
    EXPECT_FALSE(none.minus || none.plus);
}

TEST(ClimatePresentLockout, TheWaitCountsDownOnItsOwn)
{
    LockoutClock clock;
    clock.observe(Lockout{"heat", 140}, 1000);
    EXPECT_NEAR(clock.remaining(1000), 140, 1e-6);
    EXPECT_NEAR(clock.remaining(31000), 110, 1e-6);
    EXPECT_DOUBLE_EQ(clock.remaining(1000 + 200000), 0);
}

TEST(ClimatePresentLockout, TheGaugeFillsFromTheFirstValueSeen)
{
    LockoutClock clock;
    clock.observe(Lockout{"heat", 100}, 0);
    EXPECT_DOUBLE_EQ(clock.elapsedFraction(0), 0);
    EXPECT_NEAR(clock.elapsedFraction(25000), 0.25, 1e-9);

    // The server republishes the same wait a little later: the length is kept
    clock.observe(Lockout{"heat", 60}, 40000);
    EXPECT_NEAR(clock.elapsedFraction(40000), 0.4, 1e-9);
    EXPECT_NEAR(clock.elapsedFraction(70000), 0.7, 1e-9);
}

TEST(ClimatePresentLockout, ALongerWaitIsANewWait)
{
    LockoutClock clock;
    clock.observe(Lockout{"heat", 30}, 0);
    clock.observe(Lockout{"heat", 10}, 20000);
    // The relay wanted to start again and waits longer than what was left
    clock.observe(Lockout{"heat", 180}, 25000);
    EXPECT_DOUBLE_EQ(clock.elapsedFraction(25000), 0);
    EXPECT_NEAR(clock.elapsedFraction(25000 + 90000), 0.5, 1e-9);
}

TEST(ClimatePresentLockout, NoLockoutEndsTheWait)
{
    LockoutClock clock;
    clock.observe(Lockout{"heat", 100}, 0);
    clock.observe(std::nullopt, 5000);
    EXPECT_FALSE(clock.active());
    EXPECT_DOUBLE_EQ(clock.remaining(5000), 0);
    EXPECT_DOUBLE_EQ(clock.elapsedFraction(5000), 0);
}

TEST(ClimatePresentDial, TheArcRunsThreeHundredDegreesOpenAtTheBottom)
{
    EXPECT_EQ(dialAngle(0.0), 120);
    EXPECT_EQ(dialAngle(0.5), 270);
    EXPECT_EQ(dialAngle(1.0), 60);
    EXPECT_EQ(dialAngle(2.0), 60);
    EXPECT_EQ(dialAngle(-1.0), 120);
}

TEST(ClimatePresentDial, AValueMapsToItsPlaceBetweenTheBounds)
{
    const Bounds b{16, 30, 0.5, 0, "°C"};
    EXPECT_DOUBLE_EQ(dialFraction(b, 16), 0);
    EXPECT_DOUBLE_EQ(dialFraction(b, 23), 0.5);
    EXPECT_DOUBLE_EQ(dialFraction(b, 40), 1);
    EXPECT_DOUBLE_EQ(dialFraction(b, 5), 0);
}

TEST(ClimatePresentDial, TheLitPartTakesTheColourOfTheAction)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    const Values v = values({{"mode", "heat"}, {"action", "heating"}, {"current_temp", 18.0},
                             {"target_temp", 23.0}});
    const Dial d = dialFor(caps, SetpointKind::Single, v, statusOf(caps, v, true, 0));
    EXPECT_EQ(d.fill, Tone::Heat);
    EXPECT_EQ(d.knob, Tone::Heat);
    ASSERT_TRUE(d.to.has_value());
    EXPECT_DOUBLE_EQ(*d.to, 0.5);
    ASSERT_TRUE(d.room.has_value());
    EXPECT_NEAR(*d.room, 2.0 / 14.0, 1e-9);
}

TEST(ClimatePresentDial, AnIdleOrStoppedDeviceHasANeutralDial)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    for (const char* mode : {"off", "heat"})
    {
        const Values v = values({{"mode", mode}, {"action", "idle"}, {"target_temp", 23.0}});
        const Dial d = dialFor(caps, SetpointKind::Single, v, statusOf(caps, v, true, 0));
        EXPECT_EQ(d.fill, Tone::Muted) << mode;
        EXPECT_EQ(d.knob, Tone::White) << mode;
    }
}

TEST(ClimatePresentDial, AWaitingRelayIsAmberAndAnOfflineOneIsGrey)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    const Values waiting = values({{"mode", "heat"}, {"action", "idle"}, {"target_temp", 23.0}});
    EXPECT_EQ(dialFor(caps, SetpointKind::Single, waiting, statusOf(caps, waiting, true, 90)).fill, Tone::Wait);

    const Values offline = values({{"mode", "heat"}, {"target_temp", 23.0}, {"connected", false}});
    const Dial d = dialFor(caps, SetpointKind::Single, offline, statusOf(caps, offline, true, 0));
    EXPECT_EQ(d.fill, Tone::Dim);
    EXPECT_EQ(d.knob, Tone::Muted);
}

TEST(ClimatePresentDial, ARangeCarriesTwoHandlesAndLightsOnlyWhileActing)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    Values v = values({{"mode", "auto"}, {"action", "heating"}, {"target_temp_low", 20.0},
                       {"target_temp_high", 24.0}});
    Dial d = dialFor(caps, SetpointKind::Range, v, statusOf(caps, v, true, 0));
    EXPECT_FALSE(d.to.has_value());
    ASSERT_TRUE(d.low && d.high);
    EXPECT_LT(*d.low, *d.high);
    EXPECT_EQ(d.fill, Tone::Heat);
    EXPECT_EQ(d.lowKnob, Tone::Heat);
    EXPECT_EQ(d.highKnob, Tone::Cool);

    v = values({{"mode", "auto"}, {"action", "idle"}, {"target_temp_low", 20.0}, {"target_temp_high", 24.0}});
    EXPECT_EQ(dialFor(caps, SetpointKind::Range, v, statusOf(caps, v, true, 0)).fill, Tone::Dim);
}

TEST(ClimatePresentDial, AMissingSetpointDrawsNoArc)
{
    const Capabilities caps = Capabilities::parse(kCaps, kIos);
    const Values v = values({{"mode", "heat"}, {"current_temp", 20.0}});
    const Dial d = dialFor(caps, SetpointKind::Single, v, statusOf(caps, v, true, 0));
    EXPECT_FALSE(d.to.has_value());
    EXPECT_TRUE(d.room.has_value());
}

TEST(ClimatePresentMetrics, TheDialFitsItsScreen)
{
    const struct { int w, h; } screens[] = {{800, 480}, {720, 720}, {1280, 800}, {480, 800}};
    for (auto kind : {SetpointKind::Single, SetpointKind::Range, SetpointKind::None})
        for (const auto& s : screens)
        {
            const DetailMetrics m = detailMetrics(s.w, s.h, kind);
            EXPECT_GE(m.dial, 160) << s.w << "x" << s.h;
            EXPECT_LE(m.dial, 340) << s.w << "x" << s.h;
            EXPECT_LE(m.dial, s.w - 2 * m.margin) << s.w << "x" << s.h;
            EXPECT_LE(dialHeight(m.dial, m.stroke, m.knob), s.h - 2 * m.margin) << s.w << "x" << s.h;
        }
}

TEST(ClimatePresentMetrics, TheLandscapePanelPutsTheControlsBesideTheDial)
{
    const DetailMetrics wide = detailMetrics(800, 480, SetpointKind::Single);
    EXPECT_TRUE(wide.landscape);
    EXPECT_EQ(wide.columnWidth, 376);
    EXPECT_GE(wide.dial, 260);
    EXPECT_EQ(wide.button, 80);

    const DetailMetrics square = detailMetrics(720, 720, SetpointKind::Single);
    EXPECT_FALSE(square.landscape);
    EXPECT_EQ(square.dial, 340);
}

TEST(ClimatePresentMetrics, TwoPairsShareARowOnlyWhenTheyFit)
{
    EXPECT_FALSE(detailMetrics(720, 720, SetpointKind::Range).stacked);
    EXPECT_TRUE(detailMetrics(800, 480, SetpointKind::Range).stacked);
    EXPECT_TRUE(detailMetrics(480, 800, SetpointKind::Range).stacked);
    EXPECT_EQ(detailMetrics(800, 480, SetpointKind::Range).button, 64);
}

TEST(ClimatePresentValues, TheServerStateCarriesWhatTheStatusNeeds)
{
    const Values v = values({{"mode", "heat"}, {"humidity", 48.4}, {"connected", false},
                             {"fault", "sensor_stale"}, {"lockout", {{"output", "heat"}, {"remaining", 140}}}});
    ASSERT_TRUE(v.humidity.has_value());
    EXPECT_DOUBLE_EQ(*v.humidity, 48.4);
    ASSERT_TRUE(v.connected.has_value());
    EXPECT_FALSE(*v.connected);
    EXPECT_EQ(v.fault, "sensor_stale");
    ASSERT_TRUE(v.lockout.has_value());
    EXPECT_EQ(v.lockout->output, "heat");
    EXPECT_DOUBLE_EQ(v.lockout->remaining, 140);
}

TEST(ClimatePresentValues, WhatTheServerDoesNotPublishStaysAbsent)
{
    const Values v = values({{"mode", "heat"}});
    EXPECT_FALSE(v.humidity.has_value());
    EXPECT_FALSE(v.connected.has_value());
    EXPECT_TRUE(v.fault.empty());
    EXPECT_FALSE(v.lockout.has_value());
}

TEST(ClimatePresentValues, AMalformedLockoutIsIgnored)
{
    EXPECT_FALSE(values({{"lockout", "soon"}}).lockout.has_value());
    EXPECT_FALSE(values({{"lockout", {{"output", "heat"}}}}).lockout.has_value());
    EXPECT_FALSE(values({{"lockout", {{"remaining", 0}}}}).lockout.has_value());
    EXPECT_FALSE(values({{"lockout", {{"remaining", -5}}}}).lockout.has_value());
}

TEST(ClimatePresentWidgetMetrics, TheDialKeepsItsSizeInEveryWidgetOfTheTwoPanels)
{
    // Content areas: 2x2 and 3x3 of the 720x720 panel (3x3 and 4x4 grids), 2x2 of the 800x480 panel
    const struct { const char* name; int w, h; } areas[] = {
        {"2x2 on 720x720", 436, 436}, {"3x3 on 4x4 grid", 496, 496}, {"2x2 on 800x480", 360, 436},
        {"2x2 on 1280x800", 398, 500},
    };
    for (const auto& a : areas)
    {
        EXPECT_GE(widgetMetrics(a.w, a.h, SetpointKind::Single).dial, 240) << a.name;
        EXPECT_GE(widgetMetrics(a.w, a.h, SetpointKind::Range).dial, 210) << a.name;
        EXPECT_GE(widgetMetrics(a.w, a.h, SetpointKind::None).dial, 240) << a.name;
    }
}

TEST(ClimatePresentWidgetMetrics, EverythingFitsTheHeightAndTheButtonsStayFingerSized)
{
    for (int w : {300, 360, 436, 496, 700})
        for (int h : {380, 436, 496, 700})
            for (auto kind : {SetpointKind::Single, SetpointKind::Range, SetpointKind::None})
            {
                const DetailMetrics m = widgetMetrics(w, h, kind);
                const int line = kind == SetpointKind::None ? 0 : (m.stacked ? 2 * m.button + 4 : m.button);
                EXPECT_LE(m.pill + dialHeight(m.dial, m.stroke, m.knob) + line + 30 + 18, h) << w << "x" << h;
                EXPECT_GE(m.button, 56);
                EXPECT_GE(m.pill, 56);
                EXPECT_LE(m.dial, std::min(340, w));
            }
}

TEST(ClimatePresentWidgetMetrics, TwoPairsStackInAWidgetAndTheButtonsShrinkForTheDial)
{
    const DetailMetrics m = widgetMetrics(436, 436, SetpointKind::Range);
    EXPECT_TRUE(m.stacked);
    EXPECT_EQ(m.button, 64);
    // A single setpoint has the room for the big buttons
    EXPECT_EQ(widgetMetrics(436, 436, SetpointKind::Single).button, 80);
    EXPECT_FALSE(widgetMetrics(436, 436, SetpointKind::Single).stacked);
}

TEST(ClimatePresentWidgetMetrics, ANarrowWidgetKeepsTheBigNumberByTakingSmallerButtons)
{
    const DetailMetrics narrow = widgetMetrics(360, 436, SetpointKind::Single);
    EXPECT_EQ(narrow.numberPx, 80);
    EXPECT_EQ(narrow.button, 72);
    // Too narrow for the 80 px number at all: the 60 px font
    EXPECT_EQ(widgetMetrics(300, 436, SetpointKind::Single).numberPx, 60);
}

TEST(ClimatePresentLabels, ThePillOfAWidgetUsesTheShortWord)
{
    EXPECT_EQ(modeShortFr("heat"), "Chaud");
    EXPECT_EQ(modeShortFr("dry"), "Sec");
    EXPECT_EQ(modeShortFr("fan_only"), "Ventil.");
    EXPECT_EQ(modeShortFr("off"), "Arrêt");
    EXPECT_EQ(modeShortFr(""), "Inconnu");
    EXPECT_EQ(minRangeText(Bounds{16, 30, 0.5, 2, "°C"}, true), "Écart min. 2°");
}

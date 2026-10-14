#include "support/climate_fixtures.h"

using namespace climatefx;
using Op = ClimateDetailTestAccess::Op;

namespace
{

class ClimateDetailView: public ::testing::Test
{
protected:
    void SetUp() override { open(720, 720); }

    void open(int w, int h)
    {
        headless::resize(w, h);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
    }

    // Puts a device on the panel and taps it
    ClimateDetail& show(const json& device, int wc = 1, int hc = 1)
    {
        id_ = "dv_" + std::to_string(++count_);
        publish(id_, device);
        widget_ = place(id_, wc, hc, headless::width() == 800 ? kLandscape : kSquare);
        lv_obj_send_event(widget_->get(), LV_EVENT_CLICKED, nullptr);
        ClimateDetail* d = ClimateSmallWidgetTestAccess::detail(*widget_);
        EXPECT_NE(d, nullptr);
        lv_obj_update_layout(lv_layer_top());
        return *d;
    }

    lv_obj_t* overlay(ClimateDetail& d) { return ClimateDetailTestAccess::overlay(d); }
    bool text(ClimateDetail& d, const std::string& t) { return showsText(overlay(d), t); }
    void click(ClimateDetail& d, Op op, const std::string& value = "", size_t i = 0)
    {
        ClimateDetailTestAccess::click(d, op, value, i);
    }
    bool enabled(ClimateDetail& d, Op op, const std::string& value = "", size_t i = 0)
    {
        return ClimateDetailTestAccess::enabled(d, op, value, i);
    }
    bool present(ClimateDetail& d, Op op, const std::string& value = "")
    {
        return !ClimateDetailTestAccess::controls(d, op, value).empty();
    }
    void pause() { headless::advance(ClimateDetail::kCoalesceMs + 50); }

    std::string id_;
    std::unique_ptr<CalaosWidget> widget_;
    int count_ = 0;
};

} // namespace

TEST_F(ClimateDetailView, ShowsTheDialTheSetpointTheModeAndTheName)
{
    auto& d = show(salon());
    EXPECT_TRUE(text(d, "Clim salon"));
    EXPECT_TRUE(text(d, "Pièce 25.4°"));
    EXPECT_TRUE(text(d, "Refroidit"));
    // The 80 px font has no degree sign: it is lettered apart
    EXPECT_TRUE(text(d, "22"));
    EXPECT_TRUE(text(d, "°"));
    EXPECT_TRUE(text(d, "Froid"));
    EXPECT_TRUE(text(d, "Plus de réglages"));
    EXPECT_TRUE(present(d, Op::Minus));
    EXPECT_TRUE(present(d, Op::Plus));
    EXPECT_TRUE(present(d, Op::Close));
}

TEST_F(ClimateDetailView, PlusAndMinusLeaveAsOneCommandAfterAPause)
{
    Outbox out;
    auto& d = show(salon());

    click(d, Op::Plus);
    click(d, Op::Plus);
    click(d, Op::Plus);
    EXPECT_TRUE(text(d, "23.5"));
    headless::advance(ClimateDetail::kCoalesceMs - 100);
    EXPECT_TRUE(out.sent.empty());

    headless::advance(200);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.sent[0]["msg_id"], "ui_cmd:" + id_);
    EXPECT_EQ(out.command(0), (json{{"target_temp", 23.5}}));
}

TEST_F(ClimateDetailView, ARefusalShowsTheServerValueAtOnce)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Plus);
    pause();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_TRUE(text(d, "22.5"));

    receive(json{{"msg", "set_state"}, {"msg_id", "ui_cmd:" + id_}, {"data", {{"success", "false"}}}}.dump());
    EXPECT_TRUE(text(d, "22"));
    EXPECT_FALSE(text(d, "22.5"));
}

TEST_F(ClimateDetailView, AnUnansweredCommandGivesWayToTheServer)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Minus);
    pause();
    EXPECT_TRUE(text(d, "21.5"));
    headless::advance(ClimateDetail::kEchoTimeoutMs + 50);
    EXPECT_TRUE(text(d, "22"));
    EXPECT_FALSE(text(d, "21.5"));
}

TEST_F(ClimateDetailView, TheEchoSettlesTheDisplayedValue)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Plus);
    pause();
    publish(id_, withState(salon(), {{"target_temp", 22.5}}));
    headless::advance(ClimateDetail::kEchoTimeoutMs + 50);
    EXPECT_TRUE(text(d, "22.5"));
}

TEST_F(ClimateDetailView, TheServerMovesTheValueWhileTheViewIsOpen)
{
    auto& d = show(salon());
    publish(id_, withState(salon(), {{"target_temp", 25.0}, {"current_temp", 26.1}, {"action", "idle"}}));
    EXPECT_TRUE(text(d, "25"));
    EXPECT_TRUE(text(d, "Pièce 26.1°"));
    EXPECT_TRUE(text(d, "En veille"));
}

TEST_F(ClimateDetailView, PlusStopsAtTheUpperBound)
{
    Outbox out;
    auto& d = show(withState(salon(), {{"target_temp", 30.0}}));
    EXPECT_FALSE(enabled(d, Op::Plus));
    EXPECT_TRUE(enabled(d, Op::Minus));
    click(d, Op::Plus);
    pause();
    EXPECT_TRUE(out.sent.empty());
}

TEST_F(ClimateDetailView, AStoppedOrUnreachableDeviceTakesNoSetpoint)
{
    for (const json& device : {garageOff(), verandaOffline(), unknownMode()})
    {
        auto& d = show(device);
        EXPECT_FALSE(enabled(d, Op::Minus)) << device["name"];
        EXPECT_FALSE(enabled(d, Op::Plus)) << device["name"];
        widget_.reset();
    }
}

TEST_F(ClimateDetailView, AnOfflineDeviceCannotChangeMode)
{
    auto& d = show(verandaOffline());
    EXPECT_TRUE(text(d, "Hors ligne"));
    EXPECT_FALSE(enabled(d, Op::OpenMode));
}

TEST_F(ClimateDetailView, AnUnknownModeStillOffersTheChoice)
{
    auto& d = show(unknownMode());
    EXPECT_TRUE(text(d, "Mode inconnu"));
    EXPECT_TRUE(enabled(d, Op::OpenMode));
}

TEST_F(ClimateDetailView, AutoHasTwoPairsOfButtons)
{
    Outbox out;
    auto& d = show(sejour());
    EXPECT_FALSE(present(d, Op::Plus));
    for (Op op : {Op::LowMinus, Op::LowPlus, Op::HighMinus, Op::HighPlus})
        EXPECT_TRUE(present(d, op));
    EXPECT_TRUE(text(d, "19.5°"));
    EXPECT_TRUE(text(d, "24.5°"));

    click(d, Op::HighPlus);
    click(d, Op::HighPlus);
    pause();
    ASSERT_EQ(out.sent.size(), 1u);
    // A range always leaves as a pair
    EXPECT_EQ(out.command(0), (json{{"target_temp_low", 19.5}, {"target_temp_high", 25.5}}));
}

TEST_F(ClimateDetailView, TheInnerButtonsAreRefusedAtTheMinimumGap)
{
    Outbox out;
    auto& d = show(bureauAtStop());
    EXPECT_TRUE(text(d, "Écart minimum 2°"));
    EXPECT_FALSE(enabled(d, Op::LowPlus));
    EXPECT_FALSE(enabled(d, Op::HighMinus));
    EXPECT_TRUE(enabled(d, Op::LowMinus));
    EXPECT_TRUE(enabled(d, Op::HighPlus));

    click(d, Op::LowPlus);
    click(d, Op::HighMinus);
    pause();
    EXPECT_TRUE(out.sent.empty());
    EXPECT_TRUE(text(d, "21°"));
    EXPECT_TRUE(text(d, "23°"));

    // Moving the outer handle away frees the inner one
    click(d, Op::HighPlus);
    EXPECT_TRUE(enabled(d, Op::HighMinus));
    EXPECT_FALSE(text(d, "Écart minimum 2°"));
}

TEST_F(ClimateDetailView, TheMinimumGapNoteIsOnlyForAutoAtTheStop)
{
    EXPECT_FALSE(text(show(sejour()), "Écart minimum 2°"));
    widget_.reset();
    EXPECT_FALSE(text(show(salon()), "Écart minimum 2°"));
}

TEST_F(ClimateDetailView, AWaitingRelayShowsTheCountdownInSeconds)
{
    auto& d = show(waiting(140));
    EXPECT_TRUE(text(d, "Démarre dans 2\xC2\xA0min\xC2\xA0" "20"));
    headless::advance(21000);
    EXPECT_TRUE(text(d, "Démarre dans 1\xC2\xA0min\xC2\xA0" "59"));
}

TEST_F(ClimateDetailView, TheModePillOpensTheChoiceOfModes)
{
    auto& d = show(salon());
    EXPECT_FALSE(present(d, Op::Mode, "heat"));
    click(d, Op::OpenMode);

    EXPECT_TRUE(text(d, "Mode"));
    for (const char* label : {"Arrêt", "Chauffage", "Auto", "Déshumidification", "Ventilation", "Fermer"})
        EXPECT_TRUE(text(d, label)) << label;
    for (const char* mode : {"off", "heat", "cool", "auto", "dry", "fan_only"})
        EXPECT_TRUE(present(d, Op::Mode, mode)) << mode;
}

TEST_F(ClimateDetailView, OnlyTheModesTheDeviceOffersAreListed)
{
    auto& d = show(waiting());
    click(d, Op::OpenMode);
    EXPECT_TRUE(present(d, Op::Mode, "off"));
    EXPECT_TRUE(present(d, Op::Mode, "heat"));
    EXPECT_FALSE(present(d, Op::Mode, "cool"));
    EXPECT_FALSE(text(d, "Froid"));
}

TEST_F(ClimateDetailView, ChoosingAModeSendsItAtOnceAndClosesThePopup)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::OpenMode);
    click(d, Op::Mode, "heat");

    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), json("heat"));
    EXPECT_EQ(out.sent[0]["msg_id"], "ui_cmd:" + id_);
    headless::advance(50);
    EXPECT_FALSE(text(d, "Fermer"));
    EXPECT_TRUE(text(d, "Chauffage"));
}

TEST_F(ClimateDetailView, APendingSetpointLeavesWithTheMode)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Plus);
    click(d, Op::OpenMode);
    click(d, Op::Mode, "heat");

    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"mode", "heat"}, {"target_temp", 22.5}}));
    pause();
    EXPECT_EQ(out.sent.size(), 1u);
}

TEST_F(ClimateDetailView, TheCurrentModeIsMarkedInThePopup)
{
    auto& d = show(salon());
    click(d, Op::OpenMode);
    lv_obj_t* cool = ClimateDetailTestAccess::controls(d, Op::Mode, "cool").at(0);
    lv_obj_t* heat = ClimateDetailTestAccess::controls(d, Op::Mode, "heat").at(0);
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_border_color(cool, LV_PART_MAIN), lv_color_make(0x3A, 0xB4, 0xD7)));
    EXPECT_FALSE(lv_color_eq(lv_obj_get_style_border_color(heat, LV_PART_MAIN), lv_color_make(0x3A, 0xB4, 0xD7)));
}

TEST_F(ClimateDetailView, ClosingThePopupChangesNothing)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::OpenMode);
    click(d, Op::ClosePopup, "close");
    headless::advance(50);
    EXPECT_FALSE(text(d, "Fermer"));
    pause();
    EXPECT_TRUE(out.sent.empty());
}

TEST_F(ClimateDetailView, TappingOutsideThePopupClosesIt)
{
    auto& d = show(salon());
    click(d, Op::OpenMode);
    // The veil is the control with no value that is not the close button
    auto veil = ClimateDetailTestAccess::controls(d, Op::ClosePopup);
    ASSERT_EQ(veil.size(), 1u);
    lv_obj_send_event(veil[0], LV_EVENT_CLICKED, nullptr);
    headless::advance(50);
    EXPECT_FALSE(text(d, "Fermer"));
}

TEST_F(ClimateDetailView, MoreSettingsListFanSwingAndHumidity)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::OpenMore);

    EXPECT_TRUE(text(d, "Ventilation"));
    EXPECT_TRUE(text(d, "Oscillation"));
    for (const char* label : {"Silencieux", "Faible", "Moyen", "Fort", "Fixe", "Vertical", "Horizontal", "Les deux"})
        EXPECT_TRUE(text(d, label)) << label;
    EXPECT_TRUE(text(d, "Humidité 48 %"));

    click(d, Op::Fan, "low");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"fan_mode", "low"}}));
    click(d, Op::Swing, "both");
    ASSERT_EQ(out.sent.size(), 2u);
    EXPECT_EQ(out.command(1), (json{{"swing_mode", "both"}}));
    // The popup stays open: several settings can be changed
    EXPECT_TRUE(text(d, "Fermer"));
}

TEST_F(ClimateDetailView, MoreSettingsFollowTheCapabilities)
{
    {
        auto& d = show(chambre());
        click(d, Op::OpenMore);
        EXPECT_TRUE(text(d, "Ventilation"));
        EXPECT_FALSE(text(d, "Oscillation"));
        EXPECT_FALSE(showsText(overlay(d), "Humidité 48 %"));
        EXPECT_FALSE(present(d, Op::Swing, "off"));
    }
    widget_.reset();
    {
        json swingOnly = salon();
        swingOnly["device"]["capabilities"].erase("fan_modes");
        auto& d = show(swingOnly);
        click(d, Op::OpenMore);
        EXPECT_FALSE(text(d, "Ventilation"));
        EXPECT_TRUE(text(d, "Oscillation"));
    }
}

TEST_F(ClimateDetailView, PlusDeReglagesIsOnlyThereWhenThereIsSomethingToSet)
{
    EXPECT_TRUE(present(show(salon()), Op::OpenMore));
    widget_.reset();

    // A thermostat: no fan, no swing, no humidity
    EXPECT_FALSE(present(show(waiting()), Op::OpenMore));
    EXPECT_FALSE(text(*ClimateSmallWidgetTestAccess::detail(*widget_), "Plus de réglages"));
    widget_.reset();

    // Humidity alone is a reason to open it
    json humidityOnly = waiting();
    humidityOnly["device"]["state"]["humidity"] = 55;
    EXPECT_TRUE(present(show(humidityOnly), Op::OpenMore));
}

TEST_F(ClimateDetailView, TheHumidityFollowsTheServerWhileThePopupIsOpen)
{
    auto& d = show(salon());
    click(d, Op::OpenMore);
    ASSERT_TRUE(text(d, "Humidité 48 %"));
    publish(id_, withState(salon(), {{"humidity", 52}}));
    EXPECT_TRUE(text(d, "Humidité 52 %"));
}

TEST_F(ClimateDetailView, ThePopupFollowsTheServerChoice)
{
    auto& d = show(salon());
    click(d, Op::OpenMore);
    const auto borderOf = [&](const char* fan)
    {
        return lv_obj_get_style_border_color(ClimateDetailTestAccess::controls(d, Op::Fan, fan).at(0), LV_PART_MAIN);
    };
    const lv_color_t cyan = lv_color_make(0x3A, 0xB4, 0xD7);
    EXPECT_TRUE(lv_color_eq(borderOf("auto"), cyan));
    publish(id_, withState(salon(), {{"fan_mode", "high"}}));
    EXPECT_TRUE(lv_color_eq(borderOf("high"), cyan));
    EXPECT_FALSE(lv_color_eq(borderOf("auto"), cyan));
}

TEST_F(ClimateDetailView, ClosingSendsWhatIsPendingAtOnce)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Plus);
    EXPECT_TRUE(out.sent.empty());
    click(d, Op::Close);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"target_temp", 22.5}}));
}

TEST_F(ClimateDetailView, ClosingRemovesTheViewAndItsOverlay)
{
    const uint32_t before = lv_obj_get_child_count(lv_layer_top());
    auto& d = show(salon());
    lv_obj_t* ov = overlay(d);
    click(d, Op::Close);
    EXPECT_TRUE(lv_obj_has_flag(ov, LV_OBJ_FLAG_HIDDEN));

    headless::advance(50);
    EXPECT_EQ(ClimateSmallWidgetTestAccess::detail(*widget_), nullptr);
    EXPECT_EQ(lv_obj_get_child_count(lv_layer_top()), before);

    // And it opens again
    lv_obj_send_event(widget_->get(), LV_EVENT_CLICKED, nullptr);
    EXPECT_NE(ClimateSmallWidgetTestAccess::detail(*widget_), nullptr);
}

TEST_F(ClimateDetailView, AnEditInProgressIsSentWhenThePageGoes)
{
    Outbox out;
    auto& d = show(salon());
    click(d, Op::Minus);
    widget_.reset();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"target_temp", 21.5}}));
}

TEST_F(ClimateDetailView, ACommandThatCannotLeaveGoesBackToTheServerValue)
{
    // No socket: nothing can be sent
    auto& d = show(salon());
    click(d, Op::Plus);
    pause();
    EXPECT_TRUE(text(d, "22"));
    EXPECT_FALSE(text(d, "22.5"));
}

TEST_F(ClimateDetailView, ADroppedDeviceLeavesNothingToSet)
{
    auto& d = show(salon());
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "other"}}})}});
    EXPECT_FALSE(present(d, Op::Plus));
    EXPECT_TRUE(text(d, "Mode inconnu"));
}

TEST_F(ClimateDetailView, FitsTheScreenInEveryState)
{
    const json fixtures[] = {salon(), chambre(), bureauAtStop(), sejour(), waiting(), garageOff(),
                             verandaOffline(), unknownMode(), cellier(),
                             withState(salon(), {{"mode", "fan_only"}})};
    for (const auto& s : {std::pair{800, 480}, std::pair{720, 720}, std::pair{1280, 800}})
    {
        open(s.first, s.second);
        for (const auto& f : fixtures)
        {
            auto& d = show(f);
            lv_area_t screen = {0, 0, s.first - 1, s.second - 1};
            for (const auto& o : overflows(overlay(d), screen))
                ADD_FAILURE() << f["name"] << " at " << s.first << "x" << s.second << ": " << o;
            widget_.reset();
        }
    }
}

TEST_F(ClimateDetailView, ThePopupsFitTheScreen)
{
    for (const auto& s : {std::pair{800, 480}, std::pair{720, 720}})
    {
        open(s.first, s.second);
        for (Op which : {Op::OpenMode, Op::OpenMore})
        {
            auto& d = show(salon());
            click(d, which);
            lv_area_t screen = {0, 0, s.first - 1, s.second - 1};
            for (const auto& o : overflows(overlay(d), screen))
                ADD_FAILURE() << (which == Op::OpenMode ? "mode" : "more") << " at " << s.first << "x"
                              << s.second << ": " << o;
            widget_.reset();
        }
    }
}

TEST_F(ClimateDetailView, TheChoicesOfAPopupKeepAFingerSizedTarget)
{
    open(800, 480);
    auto& d = show(salon());
    click(d, Op::OpenMore);
    for (const char* fan : {"auto", "quiet", "low", "medium", "high"})
        EXPECT_GE(lv_obj_get_height(ClimateDetailTestAccess::controls(d, Op::Fan, fan).at(0)), 48) << fan;
}

TEST_F(ClimateDetailView, LandscapeAndPortraitLayoutsBothKeepTheButtonsApartAtLeast48px)
{
    for (const auto& s : {std::pair{800, 480}, std::pair{720, 720}})
    {
        open(s.first, s.second);
        for (const json& f : {salon(), bureauAtStop()})
        {
            auto& d = show(f);
            for (Op op : {Op::Plus, Op::Minus, Op::LowPlus, Op::LowMinus, Op::HighPlus, Op::HighMinus, Op::Close})
                for (lv_obj_t* b : ClimateDetailTestAccess::controls(d, op))
                {
                    EXPECT_GE(lv_obj_get_width(b), 48) << s.first;
                    EXPECT_GE(lv_obj_get_height(b), 48) << s.first;
                }
            widget_.reset();
        }
    }
}

TEST(ClimateShots, Detail)
{
    if (!std::getenv("CLIMATE_SHOTS_DIR"))
        GTEST_SKIP();

    for (const auto& s : {std::pair{800, 480}, std::pair{720, 720}})
    {
        headless::resize(s.first, s.second);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
        const Cell cell = s.first == 800 ? kLandscape : kSquare;
        const struct { const char* name; json it; Op popup; } cases[] = {
            {"salon", salon(), Op::Close}, {"sdb", waiting(), Op::Close}, {"bureau", bureauAtStop(), Op::Close},
            {"mode", salon(), Op::OpenMode}, {"more", salon(), Op::OpenMore},
        };
        for (const auto& c : cases)
        {
            publish("shot_dev", c.it);
            auto w = place("shot_dev", 1, 1, cell);
            lv_obj_send_event(w->get(), LV_EVENT_CLICKED, nullptr);
            if (c.popup != Op::Close)
                ClimateDetailTestAccess::click(*ClimateSmallWidgetTestAccess::detail(*w), c.popup);
            shootScreen(std::string("detail_") + c.name + "_" + std::to_string(s.first) + "x" + std::to_string(s.second));
        }
    }
}

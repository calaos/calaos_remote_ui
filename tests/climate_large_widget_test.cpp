#include "support/climate_fixtures.h"
#include "theme.h"

using namespace climatefx;
using Op = ClimateDetailTestAccess::Op;

namespace
{

struct Screen
{
    const char* name;
    int w, h;
    Cell cell;
};
const Screen kScreens[] = {{"800x480", 800, 480, kLandscape}, {"720x720", 720, 720, kSquare}};

class ClimateLarge: public ::testing::Test
{
protected:
    void SetUp() override { open(720, 720); }

    void open(int w, int h)
    {
        headless::resize(w, h);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
    }

    // A device on the panel, as a widget of the given size
    CalaosWidget& put(const json& device, int wc = 2, int hc = 2, Cell cell = kSquare)
    {
        id_ = "lg_" + std::to_string(++count_);
        publish(id_, device);
        widget_ = place(id_, wc, hc, cell);
        lv_obj_update_layout(lv_layer_top());
        return *widget_;
    }

    ClimateDetail& panel() { return ClimateLargeWidgetTestAccess::panel(*widget_); }
    bool text(const std::string& t) { return showsText(widget_->get(), t); }
    bool popupText(const std::string& t) { return showsText(lv_layer_top(), t); }
    void click(Op op, const std::string& value = "", size_t i = 0)
    {
        ClimateDetailTestAccess::click(panel(), op, value, i);
    }
    bool enabled(Op op, size_t i = 0) { return ClimateDetailTestAccess::enabled(panel(), op, "", i); }
    bool present(Op op, const std::string& value = "")
    {
        return !ClimateDetailTestAccess::controls(panel(), op, value).empty();
    }
    void pause() { headless::advance(ClimateDetail::kCoalesceMs + 50); }
    int dialSize() { return lv_obj_get_width(ClimateDetailTestAccess::dial(panel())); }
    lv_color_t bg() { return lv_obj_get_style_bg_color(widget_->get(), LV_PART_MAIN); }
    lv_color_t border() { return lv_obj_get_style_border_color(widget_->get(), LV_PART_MAIN); }

    std::string id_;
    std::unique_ptr<CalaosWidget> widget_;
    int count_ = 0;
};

int centreY(lv_obj_t* o)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    return (a.y1 + a.y2) / 2;
}

} // namespace

TEST_F(ClimateLarge, IsBuiltForEverySizeFrom2x2)
{
    publish("lg_reg", salon());
    for (auto [w, h] : {std::pair{2, 2}, {3, 3}, {3, 2}, {2, 3}})
    {
        auto widget = place("lg_reg", w, h);
        EXPECT_NE(dynamic_cast<ClimateLargeWidget*>(widget.get()), nullptr) << w << "x" << h;
    }
}

TEST_F(ClimateLarge, ModeAndSettingsOnTopTheDialAndTheLineInTheMiddleTheTitleAtTheBottom)
{
    put(salon());
    ClimateDetail& d = panel();
    lv_obj_t* pill = ClimateDetailTestAccess::pill(d);
    lv_obj_t* more = ClimateDetailTestAccess::controls(d, Op::OpenMore).at(0);
    lv_obj_t* dial = ClimateDetailTestAccess::dial(d);
    lv_obj_t* plus = ClimateDetailTestAccess::controls(d, Op::Plus).at(0);
    lv_obj_t* title = ClimateDetailTestAccess::titleLabel(d);

    EXPECT_LT(centreY(pill), centreY(dial));
    EXPECT_EQ(centreY(pill), centreY(more));
    EXPECT_LT(centreY(dial), centreY(plus));
    EXPECT_LT(centreY(plus), centreY(title));

    // The title sits on the widget's bottom edge, the pill on its top
    lv_area_t box, t, p;
    lv_obj_get_content_coords(widget_->get(), &box);
    lv_obj_get_coords(title, &t);
    lv_obj_get_coords(pill, &p);
    EXPECT_GE(t.y2, box.y2 - 4);
    EXPECT_LE(p.y1, box.y1 + 4);
}

TEST_F(ClimateLarge, ShowsTheWholeClimateInFrench)
{
    put(chambre());
    EXPECT_TRUE(text("PAC chambre"));
    EXPECT_TRUE(text("Pièce 18.6°"));
    EXPECT_TRUE(text("Chauffe"));
    EXPECT_TRUE(text("Chaud"));
    EXPECT_TRUE(text("20.5"));
    for (const char* english : {"Heating", "Room", "Heat", "Fan", "Swing"})
        EXPECT_FALSE(text(english)) << english;
}

TEST_F(ClimateLarge, TheTitleIsTheOnlyTextAtTheBottomAndTheModeIsTheShortWord)
{
    put(salon());
    EXPECT_TRUE(text("Froid"));
    EXPECT_TRUE(text("Clim salon"));
    // The long mode names are for the popup
    EXPECT_FALSE(text("Déshumidification"));
}

TEST_F(ClimateLarge, TheSettingsButtonIsOnlyThereWhenThereIsSomethingToSet)
{
    put(salon());
    EXPECT_TRUE(present(Op::OpenMore));
    widget_.reset();

    put(waiting());
    EXPECT_FALSE(present(Op::OpenMore));
}

TEST_F(ClimateLarge, NoChromeOfTheFullScreenViewIsThere)
{
    put(salon());
    EXPECT_FALSE(present(Op::Close));
    EXPECT_FALSE(text("Plus de réglages"));
    EXPECT_EQ(ClimateDetailTestAccess::overlay(panel()), nullptr);
}

TEST_F(ClimateLarge, PlusAndMinusLeaveAsOneCommandAfterAPause)
{
    Outbox out;
    put(salon());
    click(Op::Plus);
    click(Op::Plus);
    click(Op::Plus);
    EXPECT_TRUE(text("23.5"));
    headless::advance(ClimateDetail::kCoalesceMs - 100);
    EXPECT_TRUE(out.sent.empty());
    headless::advance(200);
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.sent[0]["msg_id"], "ui_cmd:" + id_);
    EXPECT_EQ(out.command(0), (json{{"target_temp", 23.5}}));
}

TEST_F(ClimateLarge, ARefusalShowsTheServerValueAtOnce)
{
    Outbox out;
    put(salon());
    click(Op::Plus);
    pause();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_TRUE(text("22.5"));
    receive(json{{"msg", "set_state"}, {"msg_id", "ui_cmd:" + id_}, {"data", {{"success", "false"}}}}.dump());
    EXPECT_FALSE(text("22.5"));
    EXPECT_TRUE(text("22"));
}

TEST_F(ClimateLarge, AnUnansweredCommandGivesWayToTheServer)
{
    Outbox out;
    put(salon());
    click(Op::Minus);
    pause();
    EXPECT_TRUE(text("21.5"));
    headless::advance(ClimateDetail::kEchoTimeoutMs + 50);
    EXPECT_FALSE(text("21.5"));
}

TEST_F(ClimateLarge, TheServerMovesTheValueWhileItIsShown)
{
    put(salon());
    publish(id_, withState(salon(), {{"target_temp", 25.0}, {"current_temp", 26.1}, {"action", "idle"}}));
    EXPECT_TRUE(text("25"));
    EXPECT_TRUE(text("Pièce 26.1°"));
    EXPECT_TRUE(text("En veille"));
}

TEST_F(ClimateLarge, AnEditInProgressIsSentWhenThePageGoes)
{
    Outbox out;
    put(salon());
    click(Op::Minus);
    widget_.reset();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"target_temp", 21.5}}));
}

TEST_F(ClimateLarge, AutoHasTwoPairsAndTheInnerOnesAreRefusedAtTheStop)
{
    Outbox out;
    put(bureauAtStop());
    for (Op op : {Op::LowMinus, Op::LowPlus, Op::HighMinus, Op::HighPlus})
        EXPECT_TRUE(present(op));
    EXPECT_FALSE(present(Op::Plus));
    // The note is inside the dial: the widget has no room under the line
    EXPECT_TRUE(text("Écart min. 2°"));
    EXPECT_FALSE(enabled(Op::LowPlus));
    EXPECT_FALSE(enabled(Op::HighMinus));

    click(Op::LowPlus);
    click(Op::HighMinus);
    pause();
    EXPECT_TRUE(out.sent.empty());

    click(Op::HighPlus);
    click(Op::HighPlus);
    pause();
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"target_temp_low", 21.0}, {"target_temp_high", 24.0}}));
    EXPECT_FALSE(text("Écart min. 2°"));
}

TEST_F(ClimateLarge, TheModePillOpensTheChoiceOfModesOnTheTopLayer)
{
    Outbox out;
    put(salon());
    EXPECT_FALSE(popupText("Fermer"));
    click(Op::OpenMode);
    EXPECT_TRUE(popupText("Déshumidification"));
    EXPECT_TRUE(popupText("Fermer"));
    // The popup is not a child of the widget
    EXPECT_FALSE(text("Fermer"));

    click(Op::Mode, "heat");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), json("heat"));
    headless::advance(50);
    EXPECT_FALSE(popupText("Fermer"));
}

TEST_F(ClimateLarge, ThePendingSetpointLeavesWithTheMode)
{
    Outbox out;
    put(salon());
    click(Op::Plus);
    click(Op::OpenMode);
    click(Op::Mode, "heat");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"mode", "heat"}, {"target_temp", 22.5}}));
}

TEST_F(ClimateLarge, TheSettingsButtonOpensFanSwingAndHumidity)
{
    Outbox out;
    put(salon());
    click(Op::OpenMore);
    for (const char* label : {"Ventilation", "Oscillation", "Silencieux", "Les deux", "Humidité 48 %"})
        EXPECT_TRUE(popupText(label)) << label;

    click(Op::Fan, "low");
    ASSERT_EQ(out.sent.size(), 1u);
    EXPECT_EQ(out.command(0), (json{{"fan_mode", "low"}}));
    EXPECT_TRUE(popupText("Fermer"));
}

TEST_F(ClimateLarge, APopupSurvivesTheBodyBeingRebuiltUnderIt)
{
    Outbox out;
    put(salon());
    click(Op::OpenMore);

    publish(id_, withState(salon(), {{"mode", "auto"}}));
    ASSERT_TRUE(popupText("Fermer"));

    click(Op::Fan, "low");
    ASSERT_EQ(out.sent.size(), 1u);
    click(Op::ClosePopup, "close");
    headless::advance(50);
    EXPECT_FALSE(popupText("Fermer"));
}

TEST_F(ClimateLarge, APopupSurvivesTheDeviceGoing)
{
    put(salon());
    click(Op::OpenMore);
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "other"}}})}});
    click(Op::ClosePopup, "close");
    headless::advance(50);
    EXPECT_FALSE(popupText("Fermer"));
}

TEST_F(ClimateLarge, ThePopupGoesWithTheWidget)
{
    const uint32_t before = lv_obj_get_child_count(lv_layer_top());
    put(salon());
    click(Op::OpenMore);
    EXPECT_GT(lv_obj_get_child_count(lv_layer_top()), before);
    widget_.reset();
    EXPECT_EQ(lv_obj_get_child_count(lv_layer_top()), before);
}

TEST_F(ClimateLarge, OpeningPopupsOverAndOverDoesNotGrowTheBindings)
{
    put(salon());
    click(Op::OpenMore);
    click(Op::ClosePopup, "close");
    headless::advance(50);
    const size_t once = ClimateDetailTestAccess::bindingCount(panel());
    for (int i = 0; i < 50; i++)
    {
        click(i % 2 ? Op::OpenMode : Op::OpenMore);
        click(Op::ClosePopup, "close");
        headless::advance(50);
    }
    EXPECT_EQ(ClimateDetailTestAccess::bindingCount(panel()), once);
}

TEST_F(ClimateLarge, TheWidgetIsLitInTheColourOfWhatItDoes)
{
    const struct { json item; lv_color_t bg; lv_color_t border; } rows[] = {
        {chambre(), theme_color_widget_bg_heat, theme_color_yellow},
        {salon(), theme_color_widget_bg_on, theme_color_blue},
        {bureauAtStop(), theme_color_widget_bg_off, theme_color_widget_border_off},
        {garageOff(), theme_color_widget_bg_off, theme_color_widget_border_off},
        {waiting(), theme_color_widget_bg_off, theme_color_widget_border_off},
        {verandaOffline(), theme_color_widget_bg_off, theme_color_widget_border_off},
    };
    for (const auto& r : rows)
    {
        put(r.item);
        EXPECT_TRUE(lv_color_eq(bg(), r.bg)) << r.item["name"];
        EXPECT_TRUE(lv_color_eq(border(), r.border)) << r.item["name"];
        widget_.reset();
    }
}

TEST_F(ClimateLarge, TheLightFollowsTheServer)
{
    put(salon());
    ASSERT_TRUE(lv_color_eq(bg(), theme_color_widget_bg_on));
    publish(id_, withState(salon(), {{"mode", "heat"}, {"action", "heating"}}));
    EXPECT_TRUE(lv_color_eq(bg(), theme_color_widget_bg_heat));
    publish(id_, withState(salon(), {{"action", "idle"}}));
    EXPECT_TRUE(lv_color_eq(bg(), theme_color_widget_bg_off));
}

TEST_F(ClimateLarge, AnUnreachableOrStoppedDeviceTakesNoStep)
{
    Outbox out;
    for (const json& device : {garageOff(), verandaOffline(), unknownMode()})
    {
        put(device);
        EXPECT_FALSE(enabled(Op::Minus)) << device["name"];
        EXPECT_FALSE(enabled(Op::Plus)) << device["name"];
        click(Op::Plus);
        pause();
        widget_.reset();
    }
    EXPECT_TRUE(out.sent.empty());

    put(verandaOffline());
    EXPECT_TRUE(text("Hors ligne"));
    EXPECT_FALSE(enabled(Op::OpenMode));
}

TEST_F(ClimateLarge, AWaitingRelayCountsDownByItself)
{
    put(waiting(140));
    EXPECT_TRUE(text("Démarre dans 2\xC2\xA0min\xC2\xA0" "20"));
    headless::advance(21000);
    EXPECT_TRUE(text("Démarre dans 1\xC2\xA0min\xC2\xA0" "59"));
}

TEST_F(ClimateLarge, FaultAndUnknownModeAreTold)
{
    put(withState(salon(), {{"fault", "sensor_stale"}}));
    EXPECT_TRUE(text("Sonde muette"));
    widget_.reset();
    put(unknownMode());
    EXPECT_TRUE(text("Mode inconnu"));
    EXPECT_TRUE(text("Inconnu"));
}

TEST_F(ClimateLarge, ADroppedDeviceStopsShowingItsState)
{
    put(salon());
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "other"}}})}});
    EXPECT_FALSE(text("Refroidit"));
    EXPECT_FALSE(present(Op::Plus));
    EXPECT_TRUE(text("Mode inconnu"));
}

TEST_F(ClimateLarge, ADeviceThatArrivesLaterIsBuilt)
{
    widget_ = place("lg_late", 2, 2);
    EXPECT_FALSE(present(Op::Plus));
    publish("lg_late", salon());
    EXPECT_TRUE(present(Op::Plus));
    EXPECT_TRUE(showsText(widget_->get(), "Refroidit"));
}

TEST_F(ClimateLarge, EveryFixtureFitsEveryCell)
{
    const json fixtures[] = {salon(), chambre(), bureauAtStop(), sejour(), waiting(), garageOff(),
                             verandaOffline(), unknownMode(), cellier(),
                             withState(salon(), {{"mode", "fan_only"}}),
                             withState(salon(), {{"fault", "humidity_high"}})};
    const struct { int w; int h; } sizes[] = {{2, 2}, {3, 2}, {2, 3}, {3, 3}};
    const struct { const char* name; Cell cell; } cells[] = {{"240x240", kSquare}, {"213x266", kTall},
                                                             {"200x240", kLandscape}};
    headless::resize(1280, 800);
    int n = 0;
    for (const auto& f : fixtures)
    {
        publish("lg_fit", f);
        for (const auto& c : cells)
            for (const auto& s : sizes)
            {
                auto widget = place("lg_fit", s.w, s.h, c.cell);
                lv_area_t box;
                lv_obj_get_content_coords(widget->get(), &box);
                for (const auto& o : overflows(widget->get(), box))
                    ADD_FAILURE() << f["name"] << " " << s.w << "x" << s.h << " in " << c.name << ": " << o;
                n++;
            }
    }
    EXPECT_EQ(n, 11 * 12);
}

TEST_F(ClimateLarge, TheDialStaysLargeInA2x2OnTheSmallPanel)
{
    open(800, 480);
    for (const json& f : {salon(), chambre(), garageOff(), sejour(), bureauAtStop()})
    {
        put(f, 2, 2, kLandscape);
        const bool range = f["state"] == "auto";
        EXPECT_GE(dialSize(), range ? 210 : 240) << f["name"];
        widget_.reset();
    }
}

TEST_F(ClimateLarge, TheDialStaysLargeInA2x2AndA3x3OfTheSquarePanel)
{
    for (auto [w, h] : {std::pair{2, 2}, {3, 3}})
        for (const json& f : {salon(), sejour()})
        {
            put(f, w, h, kSquare);
            const bool range = f["state"] == "auto";
            EXPECT_GE(dialSize(), range ? 210 : 240) << f["name"] << " " << w << "x" << h;
            widget_.reset();
        }
}

TEST_F(ClimateLarge, ButtonsKeepAFingerSizedTarget)
{
    for (const auto& s : kScreens)
    {
        open(s.w, s.h);
        for (const json& f : {salon(), bureauAtStop()})
        {
            put(f, 2, 2, s.cell);
            for (Op op : {Op::Plus, Op::Minus, Op::LowPlus, Op::LowMinus, Op::HighPlus, Op::HighMinus,
                          Op::OpenMore, Op::OpenMode})
                for (lv_obj_t* b : ClimateDetailTestAccess::controls(panel(), op))
                {
                    EXPECT_GE(lv_obj_get_width(b), 48) << s.name;
                    EXPECT_GE(lv_obj_get_height(b), 48) << s.name;
                }
            widget_.reset();
        }
    }
}

TEST(ClimateShots, Large)
{
    if (!std::getenv("CLIMATE_SHOTS_DIR"))
        GTEST_SKIP();

    for (const auto& s : kScreens)
    {
        headless::resize(s.w, s.h);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
        std::vector<std::unique_ptr<CalaosWidget>> widgets;
        const struct { const char* id; json it; int w, h, x, y; } cells[] = {
            {"l_chambre", chambre(), 2, 2, 0, 0},
            {"l_bureau", bureauAtStop(), 2, 2, 2, 0},
            {"l_cellier", cellier(), 1, 1, 3, 1},
        };
        for (const auto& c : cells)
        {
            if ((c.x + c.w) * s.cell.w > s.w || (c.y + c.h) * s.cell.h > s.h)
                continue;
            publish(c.id, c.it);
            widgets.push_back(place(c.id, c.w, c.h, s.cell, c.x, c.y));
        }
        shootScreen(std::string("large_") + s.name);
    }
}

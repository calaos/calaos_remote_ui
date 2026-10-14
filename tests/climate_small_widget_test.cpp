#include "support/climate_fixtures.h"
#include "theme.h"
#include "widgets/climate_large_widget.h"

using namespace climatefx;

namespace
{

struct Screen
{
    const char* name;
    int w, h;
    Cell cell;
};
const Screen kScreens[] = {{"800x480", 800, 480, kLandscape}, {"720x720", 720, 720, kSquare}};

// Every test starts on a clean 720x720 panel
class ClimateSmall: public ::testing::Test
{
protected:
    void SetUp() override
    {
        headless::resize(720, 720);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
    }
};

bool sameColor(lv_color_t a, lv_color_t b)
{
    return lv_color_eq(a, b);
}

lv_color_t colorOf(lv_obj_t* label)
{
    return lv_obj_get_style_text_color(label, LV_PART_MAIN);
}

// A read-only widget has nothing to press but itself
size_t pressableParts(CalaosWidget& w)
{
    size_t n = 0;
    visit(w.get(), [&](lv_obj_t* o)
    {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE))
            n++;
    });
    return n;
}

void tap(CalaosWidget& w)
{
    lv_obj_send_event(w.get(), LV_EVENT_CLICKED, nullptr);
}

} // namespace

TEST_F(ClimateSmall, IsBuiltForTheSmallCellsAndLeavesTheLargeOnesAlone)
{
    publish("cs_reg", salon());
    for (auto [w, h] : {std::pair{1, 1}, {2, 1}, {1, 2}, {3, 1}, {1, 3}, {4, 1}})
    {
        auto widget = place("cs_reg", w, h);
        EXPECT_NE(dynamic_cast<ClimateSmallWidget*>(widget.get()), nullptr) << w << "x" << h;
    }
    for (auto [w, h] : {std::pair{2, 2}, {3, 3}, {3, 2}})
    {
        auto widget = place("cs_reg", w, h);
        EXPECT_NE(dynamic_cast<ClimateLargeWidget*>(widget.get()), nullptr) << w << "x" << h;
    }
}

TEST_F(ClimateSmall, EveryFixtureFitsEveryCell)
{
    const json fixtures[] = {salon(), chambre(), bureauAtStop(), sejour(), waiting(), waiting(0), garageOff(),
                             verandaOffline(), unknownMode(), cellier(),
                             withState(salon(), {{"fault", "humidity_high"}}),
                             withState(salon(), {{"fault", "sensor_stale"}})};
    const struct { int w; int h; } sizes[] = {{1, 1}, {2, 1}, {1, 2}, {3, 1}, {1, 3}};
    const struct { const char* name; Cell cell; } cells[] = {{"240x240", kSquare}, {"213x266", kTall},
                                                             {"200x240", kLandscape}, {"180x180", {180, 180}}};
    headless::resize(1280, 800);
    int n = 0;
    for (const auto& f : fixtures)
    {
        publish("cs_fit", f);
        for (const auto& c : cells)
            for (const auto& s : sizes)
            {
                auto widget = place("cs_fit", s.w, s.h, c.cell);
                lv_area_t box;
                lv_obj_get_content_coords(widget->get(), &box);
                for (const auto& o : overflows(widget->get(), box))
                    ADD_FAILURE() << f["name"] << " " << s.w << "x" << s.h << " in " << c.name << ": " << o;
                n++;
            }
    }
    EXPECT_EQ(n, 12 * 20);
}

TEST_F(ClimateSmall, CompactShowsRoomSetpointStatusAndName)
{
    publish("cs_11", salon());
    auto w = place("cs_11", 1, 1);
    EXPECT_TRUE(showsText(w->get(), "25.4°"));
    EXPECT_TRUE(showsText(w->get(), "22°"));
    EXPECT_TRUE(showsText(w->get(), "Refroidit"));
    EXPECT_TRUE(showsText(w->get(), "Clim salon"));
}

TEST_F(ClimateSmall, CompactShowsARangeAsTwoValues)
{
    publish("cs_range", bureauAtStop());
    auto w = place("cs_range", 1, 1);
    EXPECT_TRUE(showsText(w->get(), "21-23°"));
    EXPECT_TRUE(showsText(w->get(), "Veille"));
}

TEST_F(ClimateSmall, CompactHasNoSetpointRowWhenTheModeHasNone)
{
    publish("cs_fan", withState(salon(), {{"mode", "fan_only"}}));
    auto w = place("cs_fan", 1, 1);
    EXPECT_FALSE(showsText(w->get(), "22°"));
    EXPECT_TRUE(showsText(w->get(), "25.4°"));
}

TEST_F(ClimateSmall, NoSizeOffersMinusOrPlus)
{
    publish("cs_ro", salon());
    for (auto [w, h] : {std::pair{1, 1}, {2, 1}, {1, 2}})
    {
        auto widget = place("cs_ro", w, h);
        // Only the widget itself can be pressed: no -/+, no mode button
        EXPECT_EQ(pressableParts(*widget), 0u) << w << "x" << h;
        EXPECT_TRUE(lv_obj_has_flag(widget->get(), LV_OBJ_FLAG_CLICKABLE));
    }
}

TEST_F(ClimateSmall, WideCarriesTheSetpointInAMiniDialAndTheRoomBeside)
{
    publish("cs_21", salon());
    auto w = place("cs_21", 2, 1);
    EXPECT_TRUE(showsText(w->get(), "22°"));
    EXPECT_TRUE(showsText(w->get(), "Clim salon"));
    EXPECT_TRUE(showsText(w->get(), "Refroidit"));
    EXPECT_TRUE(showsText(w->get(), "Pièce 25.4°"));
}

TEST_F(ClimateSmall, TallCarriesTheSameThingsStacked)
{
    publish("cs_12", chambre());
    auto w = place("cs_12", 1, 2);
    EXPECT_TRUE(showsText(w->get(), "20.5°"));
    EXPECT_TRUE(showsText(w->get(), "Pièce 18.6°"));
    EXPECT_TRUE(showsText(w->get(), "Chauffe"));
    EXPECT_TRUE(showsText(w->get(), "PAC chambre"));
}

TEST_F(ClimateSmall, ARangeShowsBothHandlesInTheMiniDial)
{
    publish("cs_auto", sejour());
    auto w = place("cs_auto", 2, 1);
    EXPECT_TRUE(showsText(w->get(), "19.5°"));
    EXPECT_TRUE(showsText(w->get(), "24.5°"));
}

TEST_F(ClimateSmall, AWaitingRelayShowsTheCountdownAndItsGauge)
{
    publish("cs_wait", waiting());
    auto wide = place("cs_wait", 2, 1);
    EXPECT_TRUE(showsText(wide->get(), "Démarre dans 3\xC2\xA0min"));

    auto compact = place("cs_wait", 1, 1, kSquare, 1, 1);
    EXPECT_TRUE(showsText(compact->get(), "Attente"));
    EXPECT_FALSE(showsText(compact->get(), "Démarre dans 3\xC2\xA0min"));
}

TEST_F(ClimateSmall, ATallCellShortensAWaitThatWouldTakeThreeLines)
{
    publish("cs_tall_wait", waiting());
    auto roomy = place("cs_tall_wait", 1, 2, kSquare);
    EXPECT_TRUE(showsText(roomy->get(), "Démarre dans 3\xC2\xA0min"));
    roomy.reset();

    headless::resize(800, 480);
    auto narrow = place("cs_tall_wait", 1, 2, kLandscape);
    EXPECT_FALSE(showsText(narrow->get(), "Démarre dans 3\xC2\xA0min"));
    EXPECT_TRUE(showsText(narrow->get(), "Attente"));
}

TEST_F(ClimateSmall, TheCountdownRunsByItselfBetweenPublications)
{
    publish("cs_count", waiting(140));
    auto w = place("cs_count", 2, 1);
    ASSERT_TRUE(showsText(w->get(), "Démarre dans 3\xC2\xA0min"));

    headless::advance(30000);
    EXPECT_TRUE(showsText(w->get(), "Démarre dans 2\xC2\xA0min"));
    headless::advance(80000);
    EXPECT_TRUE(showsText(w->get(), "Démarre dans 1\xC2\xA0min"));

    // Past the end the wait is over: the status falls back to the action
    headless::advance(40000);
    EXPECT_FALSE(showsText(w->get(), "Démarre dans 1\xC2\xA0min"));
    EXPECT_TRUE(showsText(w->get(), "En veille"));
}

TEST_F(ClimateSmall, TheGaugeFillsAsTheWaitGoes)
{
    publish("cs_gauge", waiting(100));
    auto w = place("cs_gauge", 2, 1);

    const auto fill = [&]()
    {
        int width = -1;
        visit(w->get(), [&](lv_obj_t* o)
        {
            // The fill is the only 3 px high child of a 3 px high track
            if (shown(o) && lv_obj_get_height(o) == 3 && lv_obj_get_height(lv_obj_get_parent(o)) == 3)
                width = lv_obj_get_width(o);
        });
        return width;
    };
    EXPECT_EQ(fill(), 0);
    headless::advance(50000);
    EXPECT_GT(fill(), 0);
}

TEST_F(ClimateSmall, ANewPublicationRestartsTheCountdown)
{
    publish("cs_repub", waiting(140));
    auto w = place("cs_repub", 2, 1);
    headless::advance(30000);
    publish("cs_repub", waiting(60));
    EXPECT_TRUE(showsText(w->get(), "Démarre dans 1\xC2\xA0min"));
}

TEST_F(ClimateSmall, StatesAreToldInWordsAndColour)
{
    const struct { json item; const char* word; lv_color_t color; } rows[] = {
        {garageOff(), "Arrêté", theme_color_muted},
        {verandaOffline(), "Hors ligne", theme_color_red},
        {unknownMode(), "Inconnu", theme_color_muted},
        {chambre(), "Chauffe", theme_color_yellow},
        {salon(), "Refroidit", theme_color_blue},
        {withState(salon(), {{"fault", "sensor_stale"}}), "Sonde muette", theme_color_amber},
    };
    for (const auto& r : rows)
    {
        publish("cs_words", r.item);
        auto w = place("cs_words", 1, 1);
        auto found = labels(w->get(), r.word);
        ASSERT_EQ(found.size(), 1u) << r.word;
        EXPECT_TRUE(sameColor(colorOf(found[0]), r.color)) << r.word;
    }
}

TEST_F(ClimateSmall, TheWidgetIsLitInTheColourOfWhatItDoes)
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
        publish("cs_lit", r.item);
        auto w = place("cs_lit", 1, 1);
        EXPECT_TRUE(sameColor(lv_obj_get_style_bg_color(w->get(), LV_PART_MAIN), r.bg)) << r.item["name"];
        EXPECT_TRUE(sameColor(lv_obj_get_style_border_color(w->get(), LV_PART_MAIN), r.border)) << r.item["name"];
    }
}

TEST_F(ClimateSmall, ARepublishedStateIsFollowed)
{
    publish("cs_follow", salon());
    auto w = place("cs_follow", 1, 1);
    ASSERT_TRUE(showsText(w->get(), "Refroidit"));

    publish("cs_follow", withState(salon(), {{"action", "idle"}, {"target_temp", 24.5}, {"current_temp", 23.0}}));
    EXPECT_FALSE(showsText(w->get(), "Refroidit"));
    EXPECT_TRUE(showsText(w->get(), "Veille"));
    EXPECT_TRUE(showsText(w->get(), "24.5°"));
    EXPECT_TRUE(showsText(w->get(), "23°"));
}

TEST_F(ClimateSmall, ADroppedDeviceStopsShowingItsState)
{
    publish("cs_drop", salon());
    auto w = place("cs_drop", 1, 1);
    ASSERT_TRUE(showsText(w->get(), "Refroidit"));

    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "cs_other"}}})}});
    EXPECT_FALSE(showsText(w->get(), "Refroidit"));
    EXPECT_FALSE(showsText(w->get(), "25.4°"));
}

TEST_F(ClimateSmall, ADeviceThatArrivesLaterIsBuilt)
{
    auto w = place("cs_late", 2, 1);
    EXPECT_FALSE(showsText(w->get(), "Refroidit"));
    publish("cs_late", salon());
    EXPECT_TRUE(showsText(w->get(), "Refroidit"));
    EXPECT_TRUE(showsText(w->get(), "22°"));
}

TEST_F(ClimateSmall, ATapOpensTheFullScreenView)
{
    Outbox out;
    publish("cs_tap", salon());
    auto w = place("cs_tap", 1, 1);
    EXPECT_EQ(ClimateSmallWidgetTestAccess::detail(*w), nullptr);

    tap(*w);
    ClimateDetail* d = ClimateSmallWidgetTestAccess::detail(*w);
    ASSERT_NE(d, nullptr);
    lv_obj_t* overlay = ClimateDetailTestAccess::overlay(*d);
    EXPECT_EQ(lv_obj_get_parent(overlay), lv_layer_top());
    lv_obj_update_layout(overlay);
    EXPECT_EQ(lv_obj_get_width(overlay), 720);
    EXPECT_EQ(lv_obj_get_height(overlay), 720);
    // Opening is not a command
    headless::advance(ClimateDetail::kCoalesceMs + 100);
    EXPECT_TRUE(out.sent.empty());

    // A second tap on the dimmed widget does not stack a second view
    tap(*w);
    EXPECT_EQ(ClimateSmallWidgetTestAccess::detail(*w), d);
}

TEST_F(ClimateSmall, EverySmallSizeOpensTheView)
{
    publish("cs_sizes", salon());
    for (auto [wc, hc] : {std::pair{1, 1}, {2, 1}, {1, 2}})
    {
        auto w = place("cs_sizes", wc, hc);
        tap(*w);
        EXPECT_NE(ClimateSmallWidgetTestAccess::detail(*w), nullptr) << wc << "x" << hc;
    }
}

TEST_F(ClimateSmall, WithoutADeviceThereIsNothingToOpen)
{
    auto w = place("cs_none", 1, 1);
    tap(*w);
    EXPECT_EQ(ClimateSmallWidgetTestAccess::detail(*w), nullptr);
    EXPECT_FALSE(lv_obj_has_flag(w->get(), LV_OBJ_FLAG_CLICKABLE));
}

TEST_F(ClimateSmall, TheViewGoesWithTheWidget)
{
    publish("cs_gone", salon());
    const uint32_t before = lv_obj_get_child_count(lv_layer_top());
    {
        auto w = place("cs_gone", 1, 1);
        tap(*w);
        EXPECT_EQ(lv_obj_get_child_count(lv_layer_top()), before + 1);
    }
    EXPECT_EQ(lv_obj_get_child_count(lv_layer_top()), before);
}

TEST_F(ClimateSmall, TheViewStaysUnderWhatMustAlwaysBeOnTop)
{
    lv_obj_t* toast = lv_obj_create(lv_layer_top());
    publish("cs_z", salon());
    auto w = place("cs_z", 1, 1);
    tap(*w);
    lv_obj_t* overlay = ClimateDetailTestAccess::overlay(*ClimateSmallWidgetTestAccess::detail(*w));
    EXPECT_LT(lv_obj_get_index(overlay), lv_obj_get_index(toast));
    w.reset();
    lv_obj_delete(toast);
}

TEST(ClimateShots, Captures)
{
    if (!std::getenv("CLIMATE_SHOTS_DIR"))
        GTEST_SKIP();

    for (const auto& s : kScreens)
    {
        headless::resize(s.w, s.h);
        lv_obj_set_style_bg_color(headless::screen(), lv_color_make(0x17, 0x17, 0x17), 0);
        const struct { const char* id; json it; int w, h, x, y; } cells[] = {
            {"a_salon", salon(), 1, 1, 0, 0},
            {"a_sdb", waiting(), 2, 1, 1, 0},
            {"a_chambre", chambre(), 1, 2, 0, 1},
            {"a_garage", garageOff(), 1, 1, 1, 1},
            {"a_veranda", verandaOffline(), 1, 1, 2, 1},
            {"a_cuisine", unknownMode(), 1, 1, 3, 0},
            {"a_bureau", bureauAtStop(), 2, 1, 2, 2},
        };
        std::vector<std::unique_ptr<CalaosWidget>> widgets;
        for (const auto& c : cells)
        {
            if ((c.x + c.w) * s.cell.w > s.w || (c.y + c.h) * s.cell.h > s.h)
                continue;
            publish(c.id, c.it);
            widgets.push_back(place(c.id, c.w, c.h, s.cell, c.x, c.y));
        }
        shootScreen(std::string("grid_") + s.name);
    }
}

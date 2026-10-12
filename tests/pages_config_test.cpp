#include <gtest/gtest.h>
#include "calaos_protocol.h"
#include "board_config.h"

using CalaosProtocol::PagesConfig;
using CalaosProtocol::RemoteUIConfig;

namespace
{
constexpr int kBoardGridW = BOARD_PREFERED_GRID_WIDTH;
constexpr int kBoardGridH = BOARD_PREFERED_GRID_HEIGHT;
}

TEST(PagesConfig, DefaultGridComesFromBoard)
{
    PagesConfig c;
    EXPECT_EQ(c.grid_width, kBoardGridW);
    EXPECT_EQ(c.grid_height, kBoardGridH);
    EXPECT_TRUE(c.pages.empty());
}

TEST(PagesConfig, ParsesGridPagesAndWidgets)
{
    auto c = PagesConfig::fromJson(R"({
        "grid_width": 4, "grid_height": 5,
        "pages": [
            {"widgets": [
                {"io_id": "light_1", "type": "LightSwitch", "name": "Salon", "x": 1, "y": 2, "w": 2, "h": 3},
                {"type": "Clock"}
            ]},
            {"widgets": []}
        ]})");

    EXPECT_EQ(c.grid_width, 4);
    EXPECT_EQ(c.grid_height, 5);
    ASSERT_EQ(c.pages.size(), 2u);
    ASSERT_EQ(c.pages[0].widgets.size(), 2u);

    const auto& w = c.pages[0].widgets[0];
    EXPECT_EQ(w.io_id, "light_1");
    EXPECT_EQ(w.type, "LightSwitch");
    EXPECT_EQ(w.override_name, "Salon");
    EXPECT_EQ(w.x, 1);
    EXPECT_EQ(w.y, 2);
    EXPECT_EQ(w.w, 2);
    EXPECT_EQ(w.h, 3);

    const auto& clock = c.pages[0].widgets[1];
    EXPECT_TRUE(clock.io_id.empty());
    EXPECT_EQ(clock.x, 0);
    EXPECT_EQ(clock.y, 0);
    EXPECT_EQ(clock.w, 1);
    EXPECT_EQ(clock.h, 1);
    EXPECT_TRUE(c.pages[1].widgets.empty());
}

TEST(PagesConfig, MissingGridKeepsBoardDefaults)
{
    auto c = PagesConfig::fromJson(R"({"pages": []})");
    EXPECT_EQ(c.grid_width, kBoardGridW);
    EXPECT_EQ(c.grid_height, kBoardGridH);
}

TEST(PagesConfig, NonPositiveGridFallsBackToThreeNotBoardDefault)
{
    auto c = PagesConfig::fromJson(R"({"grid_width": 0, "grid_height": -2})");
    EXPECT_EQ(c.grid_width, 3);
    EXPECT_EQ(c.grid_height, 3);
}

TEST(PagesConfig, NumericFieldsAcceptStrings)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"type": "T", "x": "1", "y": "2", "w": "3", "h": "4"}]}]})");
    const auto& w = c.pages.at(0).widgets.at(0);
    EXPECT_EQ(w.x, 1);
    EXPECT_EQ(w.y, 2);
    EXPECT_EQ(w.w, 3);
    EXPECT_EQ(w.h, 4);
}

TEST(PagesConfig, WidthHeightAreAliasesAndShortKeysWin)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"type": "A", "width": 2, "height": "3"},
        {"type": "B", "w": 4, "width": 9, "h": 5, "height": 9}]}]})");
    const auto& a = c.pages.at(0).widgets.at(0);
    EXPECT_EQ(a.w, 2);
    EXPECT_EQ(a.h, 3);
    const auto& b = c.pages.at(0).widgets.at(1);
    EXPECT_EQ(b.w, 4);
    EXPECT_EQ(b.h, 5);
}

TEST(PagesConfig, UnknownScalarKeysBecomeParams)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [{
        "type": "Clock", "clock_timezone": "Europe/Paris", "clock_format": 24,
        "show_date": true, "hidden": false, "nested": {"a": 1}, "list": [1], "nothing": null,
        "width": 2}]}]})");
    const auto& p = c.pages.at(0).widgets.at(0).params;

    EXPECT_EQ(p.at("clock_timezone"), "Europe/Paris");
    EXPECT_EQ(p.at("clock_format"), "24");
    EXPECT_EQ(p.at("show_date"), "true");
    EXPECT_EQ(p.at("hidden"), "false");
    EXPECT_EQ(p.count("nested"), 0u);
    EXPECT_EQ(p.count("list"), 0u);
    EXPECT_EQ(p.count("nothing"), 0u);
    EXPECT_EQ(p.count("width"), 0u);
    EXPECT_EQ(p.size(), 4u);
}

TEST(PagesConfig, FractionalParamIsTruncatedToInt)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [{"type": "T", "ratio": 1.9}]}]})");
    EXPECT_EQ(c.pages.at(0).widgets.at(0).params.at("ratio"), "1");
}

TEST(PagesConfig, WidgetWithoutTypeIsSkipped)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"io_id": "a"}, {"io_id": "b", "type": ""}, {"io_id": "c", "type": "T"}]}]})");
    ASSERT_EQ(c.pages.at(0).widgets.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets[0].io_id, "c");
}

TEST(PagesConfig, WidgetWithNonPositiveSizeIsSkipped)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"type": "T", "w": 0}, {"type": "T", "h": -1}, {"type": "T", "width": 0}, {"type": "ok"}]}]})");
    ASSERT_EQ(c.pages.at(0).widgets.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets[0].type, "ok");
}

TEST(PagesConfig, PageWithoutWidgetsKeyIsStillAPage)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{}, {"widgets": 3}]})");
    ASSERT_EQ(c.pages.size(), 2u);
    EXPECT_TRUE(c.pages[0].widgets.empty());
    EXPECT_TRUE(c.pages[1].widgets.empty());
}

TEST(PagesConfig, PagesThatIsNotAnArrayIsIgnored)
{
    auto c = PagesConfig::fromJson(R"({"grid_width": 2, "pages": {"widgets": []}})");
    EXPECT_EQ(c.grid_width, 2);
    EXPECT_TRUE(c.pages.empty());
}

TEST(PagesConfig, InvalidJsonReturnsBoardDefaults)
{
    auto c = PagesConfig::fromJson("{not json");
    EXPECT_EQ(c.grid_width, kBoardGridW);
    EXPECT_EQ(c.grid_height, kBoardGridH);
    EXPECT_TRUE(c.pages.empty());
}

TEST(PagesConfig, EmptyStringReturnsBoardDefaults)
{
    auto c = PagesConfig::fromJson("");
    EXPECT_EQ(c.grid_width, kBoardGridW);
    EXPECT_TRUE(c.pages.empty());
}

TEST(PagesConfig, MistypedWidgetFieldDropsOnlyThatWidget)
{
    auto c = PagesConfig::fromJson(R"({"grid_width": 6, "pages": [
        {"widgets": [{"type": "ok"}, {"type": "T", "io_id": 5}, {"type": "ok2"}]},
        {"widgets": [{"type": "page2"}]}]})");
    EXPECT_EQ(c.grid_width, 6);
    ASSERT_EQ(c.pages.size(), 2u);
    ASSERT_EQ(c.pages[0].widgets.size(), 2u);
    EXPECT_EQ(c.pages[0].widgets[0].type, "ok");
    EXPECT_EQ(c.pages[0].widgets[1].type, "ok2");
    ASSERT_EQ(c.pages[1].widgets.size(), 1u);
    EXPECT_EQ(c.pages[1].widgets[0].type, "page2");
}

TEST(PagesConfig, WidgetFieldsOfTheWrongTypeAreDropped)
{
    auto c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"type": 3}, {"type": "T", "name": []}, {"type": "T", "x": null}, {"type": "T", "w": {}},
        {"type": "T", "h": true}, "stray", 7, null, {"type": "kept"}]}]})");
    ASSERT_EQ(c.pages.size(), 1u);
    ASSERT_EQ(c.pages[0].widgets.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets[0].type, "kept");
}

TEST(PagesConfig, GridGivenAsStringIsAccepted)
{
    auto c = PagesConfig::fromJson(R"({"grid_width": "4", "grid_height": " 5", "pages": [{"widgets": [{"type": "ok"}]}]})");
    EXPECT_EQ(c.grid_width, 4);
    EXPECT_EQ(c.grid_height, 5);
    ASSERT_EQ(c.pages.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets.size(), 1u);
}

TEST(PagesConfig, UnusableGridKeepsTheBoardDefaultAndThePages)
{
    auto c = PagesConfig::fromJson(R"({"grid_width": "abc", "grid_height": [2], "pages": [{"widgets": [{"type": "ok"}]}]})");
    EXPECT_EQ(c.grid_width, kBoardGridW);
    EXPECT_EQ(c.grid_height, kBoardGridH);
    ASSERT_EQ(c.pages.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets.size(), 1u);
}

TEST(PagesConfig, NonNumericStringCoordinateDropsOnlyThatWidget)
{
    PagesConfig c;
    ASSERT_NO_THROW(c = PagesConfig::fromJson(R"({"pages": [{"widgets": [
        {"type": "bad", "x": "abc"}, {"type": "bad", "y": ""}, {"type": "bad", "w": "2x"},
        {"type": "bad", "height": "99999999999"}, {"type": "ok", "x": "1"}]}]})"));
    ASSERT_EQ(c.pages.size(), 1u);
    ASSERT_EQ(c.pages[0].widgets.size(), 1u);
    EXPECT_EQ(c.pages[0].widgets[0].type, "ok");
    EXPECT_EQ(c.pages[0].widgets[0].x, 1);
}

TEST(RemoteUIConfig, EmptyPagesJsonYieldsDefaultPages)
{
    RemoteUIConfig cfg;
    auto p = cfg.getParsedPages();
    EXPECT_EQ(p.grid_width, kBoardGridW);
    EXPECT_TRUE(p.pages.empty());
}

TEST(RemoteUIConfig, ParsesPagesJson)
{
    RemoteUIConfig cfg;
    cfg.pages_json = R"({"grid_width": 2, "grid_height": 2, "pages": [{"widgets": [{"type": "T"}]}]})";
    auto p = cfg.getParsedPages();
    EXPECT_EQ(p.grid_width, 2);
    ASSERT_EQ(p.pages.size(), 1u);
    EXPECT_EQ(p.pages[0].widgets.size(), 1u);
}

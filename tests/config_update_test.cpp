#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_store.h"

using nlohmann::json;
using CalaosProtocol::MSG_CONFIG_UPDATE;

namespace
{
const ConfigUpdateReceivedData* configOf(const std::vector<AppEvent>& events)
{
    return firstData<ConfigUpdateReceivedData>(events, AppEventType::ConfigUpdateReceived);
}

const IoStatesReceivedData* batchOf(const std::vector<AppEvent>& events)
{
    return firstData<IoStatesReceivedData>(events, AppEventType::IoStatesReceived);
}
}

TEST(ConfigUpdate, ParsesScalarsAndDefaults)
{
    auto events = receive(MSG_CONFIG_UPDATE, json::object());
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->config.name, "");
    EXPECT_EQ(c->config.theme, "dark");
    EXPECT_EQ(c->config.brightness, 80);
    EXPECT_EQ(c->config.timeout, 30);
    EXPECT_EQ(c->config.screensaver_timeout, 0);
    EXPECT_EQ(c->config.screensaver_mode, "black");
}

TEST(ConfigUpdate, RoomIsAStringOrAnObjectWithAName)
{
    EXPECT_EQ(configOf(receive(MSG_CONFIG_UPDATE, {{"room", "Salon"}}))->config.room, "Salon");
    EXPECT_EQ(configOf(receive(MSG_CONFIG_UPDATE, {{"room", {{"name", "Cuisine"}}}}))->config.room, "Cuisine");
}

TEST(ConfigUpdate, ServerTypoBrigtnessIsAccepted)
{
    EXPECT_EQ(configOf(receive(MSG_CONFIG_UPDATE, {{"brigtness", 55}}))->config.brightness, 55);
    EXPECT_EQ(configOf(receive(MSG_CONFIG_UPDATE, {{"brightness", 66}}))->config.brightness, 66);
    EXPECT_EQ(configOf(receive(MSG_CONFIG_UPDATE, {{"brigtness", 10}, {"brightness", 90}}))->config.brightness, 10);
}

TEST(ConfigUpdate, ScreensaverNumbersAcceptStringsAndEmptyMeansZero)
{
    auto* a = configOf(receive(MSG_CONFIG_UPDATE, {{"screensaver_timeout", "120"}, {"screensaver_dimming", 20}}));
    EXPECT_EQ(a->config.screensaver_timeout, 120);
    EXPECT_EQ(a->config.screensaver_dimming, 20);

    auto* b = configOf(receive(MSG_CONFIG_UPDATE, {{"screensaver_timeout", ""}}));
    EXPECT_EQ(b->config.screensaver_timeout, 0);
}

TEST(ConfigUpdate, PagesJsonCarriesGridAndPages)
{
    auto events = receive(MSG_CONFIG_UPDATE, {
        {"grid_width", 4}, {"grid_height", 2},
        {"pages", json::array({{{"widgets", json::array({{{"type", "LightSwitch"}, {"io_id", "cfg_l1"}}})}}})}});
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);

    auto pages = c->config.getParsedPages();
    EXPECT_EQ(pages.grid_width, 4);
    EXPECT_EQ(pages.grid_height, 2);
    ASSERT_EQ(pages.pages.size(), 1u);
    ASSERT_EQ(pages.pages[0].widgets.size(), 1u);
    EXPECT_EQ(pages.pages[0].widgets[0].io_id, "cfg_l1");
}

TEST(ConfigUpdate, MissingGridDefaultsToThreeByThree)
{
    auto pages = configOf(receive(MSG_CONFIG_UPDATE, json::object()))->config.getParsedPages();
    EXPECT_EQ(pages.grid_width, 3);
    EXPECT_EQ(pages.grid_height, 3);
    EXPECT_TRUE(pages.pages.empty());
}

TEST(ConfigUpdate, IoItemsBecomeOneBatchWithDefaultFalseState)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"io_items", json::array({
        {{"id", "cfg_a"}, {"type", "WODigital"}, {"gui_type", "light"}, {"name", "A"}, {"visible", "true"}, {"rw", "true"}},
        {{"id", "cfg_b"}, {"visible", "false"}, {"rw", "false"}}})}});

    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->ioStates.size(), 2u);

    const auto& a = b->ioStates.at("cfg_a");
    EXPECT_EQ(a.type, "WODigital");
    EXPECT_EQ(a.gui_type, "light");
    EXPECT_EQ(a.name, "A");
    EXPECT_EQ(a.state, "false");
    EXPECT_TRUE(a.visible);
    EXPECT_TRUE(a.enabled);

    const auto& bb = b->ioStates.at("cfg_b");
    EXPECT_FALSE(bb.visible);
    EXPECT_FALSE(bb.enabled);
}

TEST(ConfigUpdate, IoItemsFlagsAreStringsAndAnythingButTrueIsFalse)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"io_items", json::array({
        {{"id", "cfg_c"}, {"visible", "True"}, {"rw", "1"}}})}});
    const auto& c = batchOf(events)->ioStates.at("cfg_c");
    EXPECT_FALSE(c.visible);
    EXPECT_FALSE(c.enabled);
}

TEST(ConfigUpdate, NoIoItemsMeansNoIoStatesEvent)
{
    EXPECT_EQ(batchOf(receive(MSG_CONFIG_UPDATE, json::object())), nullptr);
}

TEST(ConfigUpdate, EventsArriveInTheStore)
{
    receive(MSG_CONFIG_UPDATE, {{"name", "Panel"}, {"io_items", json::array({{{"id", "cfg_store"}, {"name", "Store"}}})}});
    auto st = AppStore::getInstance().getStateCopy();
    EXPECT_EQ(st.config.name, "Panel");
    ASSERT_EQ(st.ioStates.count("cfg_store"), 1u);
    EXPECT_EQ(st.ioStates.at("cfg_store").name, "Store");
}

// The server sends these flags as strings; a JSON boolean must not drop the config update.
TEST(ConfigUpdate, BooleanIoItemFlagsAreAcceptedAndTheConfigIsKept)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"name", "Kept"},
        {"io_items", json::array({{{"id", "cfg_bool"}, {"visible", true}, {"rw", false}}})}});
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->config.name, "Kept");
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->ioStates.at("cfg_bool").visible);
    EXPECT_FALSE(b->ioStates.at("cfg_bool").enabled);
}

TEST(ConfigUpdate, BadIoItemIsSkippedAndTheOthersAndTheConfigAreKept)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"name", "Kept"},
        {"io_items", json::array({
            "stray", 3, {{"id", 9}, {"name", "numeric id"}},
            {{"id", "cfg_bad_name"}, {"name", json::object()}, {"type", json::array()}},
            {{"id", "cfg_good"}, {"name", "Good"}}})}});
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->config.name, "Kept");
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 2u);
    EXPECT_TRUE(b->ioStates.at("cfg_bad_name").name.empty());
    EXPECT_EQ(b->ioStates.at("cfg_good").name, "Good");
}

TEST(ConfigUpdate, ScalarOfTheWrongTypeKeepsItsDefaultAndTheConfig)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"name", "Kept"}, {"brightness", "abc"},
        {"timeout", json::object()}, {"screensaver_timeout", "abc"}, {"screensaver_dimming", nullptr},
        {"theme", 4}});
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->config.name, "Kept");
    EXPECT_EQ(c->config.brightness, 80);
    EXPECT_EQ(c->config.timeout, 30);
    EXPECT_EQ(c->config.screensaver_timeout, 0);
    EXPECT_EQ(c->config.screensaver_dimming, 0);
}

TEST(ConfigUpdate, GridGivenAsStringsIsAccepted)
{
    auto events = receive(MSG_CONFIG_UPDATE, {{"grid_width", "4"}, {"grid_height", "2"}});
    auto* c = configOf(events);
    ASSERT_NE(c, nullptr);
    auto pages = c->config.getParsedPages();
    EXPECT_EQ(pages.grid_width, 4);
    EXPECT_EQ(pages.grid_height, 2);
}

namespace
{
bool isStored(const std::string& id)
{
    return AppStore::getInstance().getStateCopy().ioStates.count(id) > 0;
}

json climateItem(const std::string& id)
{
    return {{"state", "heat"}, {"gui_type", "climate"},
            {"device", {{"family", "climate"}, {"impl", "thermostat"},
                        {"capabilities", {{"modes", {"off", "heat"}}}}, {"state", {{"mode", "heat"}}}}}};
}
}

// The server re-sends its config at every connection, followed by the full
// io_states batch: what the panel no longer references must not survive it.
TEST(ConfigUpdateReset, AnIoTheConfigNoLongerListsIsDropped)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{"reset_gone", climateItem("reset_gone")}, {"reset_kept", "true"}});
    ASSERT_TRUE(isStored("reset_gone"));

    receive(MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "reset_kept"}}})}});

    EXPECT_FALSE(isStored("reset_gone"));
    ASSERT_TRUE(isStored("reset_kept"));
    EXPECT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("reset_kept").state, "true");
}

TEST(ConfigUpdateReset, AnEmptyListLeavesNoIo)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{"reset_lonely", climateItem("reset_lonely")}});
    receive(MSG_CONFIG_UPDATE, {{"io_items", json::array()}});
    EXPECT_TRUE(AppStore::getInstance().getStateCopy().ioStates.empty());
}

TEST(ConfigUpdateReset, AnUnreadableListDropsNothing)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{"reset_safe", "true"}});
    receive(MSG_CONFIG_UPDATE, {{"name", "No list"}});
    receive(MSG_CONFIG_UPDATE, {{"io_items", "not a list"}});
    EXPECT_TRUE(isStored("reset_safe"));
}

TEST(ConfigUpdateReset, SubscribersSeeTheDroppedIoGone)
{
    receive(CalaosProtocol::MSG_IO_STATES, {{"reset_seen", climateItem("reset_seen")}});

    bool sawItGone = false;
    SubscriptionId id = AppStore::getInstance().subscribe([&](const AppState& s)
    {
        if (!s.ioStates.count("reset_seen"))
            sawItGone = true;
    });
    receive(MSG_CONFIG_UPDATE, {{"io_items", json::array({{{"id", "reset_other"}}})}});
    AppStore::getInstance().unsubscribe(id);

    EXPECT_TRUE(sawItGone);
}

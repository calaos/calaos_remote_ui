#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_store.h"

using nlohmann::json;
using CalaosProtocol::MSG_IO_STATES;

namespace
{
const IoStatesReceivedData* batchOf(const std::vector<AppEvent>& events)
{
    return firstData<IoStatesReceivedData>(events, AppEventType::IoStatesReceived);
}

int countBatches(const std::vector<AppEvent>& events)
{
    int n = 0;
    for (const auto& e : events)
        if (e.getType() == AppEventType::IoStatesReceived)
            n++;
    return n;
}

bool storeHas(const std::string& id)
{
    return AppStore::getInstance().getStateCopy().ioStates.count(id) > 0;
}
}

TEST(IoStatesArray, ParsesAllFields)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "arr_full"}, {"type", "WODigital"}, {"state", "true"}, {"gui_type", "light"},
         {"name", "Salon"}, {"visible", false}, {"enabled", false}}}));

    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->ioStates.size(), 1u);
    const auto& io = b->ioStates.at("arr_full");
    EXPECT_EQ(io.id, "arr_full");
    EXPECT_EQ(io.type, "WODigital");
    EXPECT_EQ(io.state, "true");
    EXPECT_EQ(io.gui_type, "light");
    EXPECT_EQ(io.name, "Salon");
    EXPECT_FALSE(io.visible);
    EXPECT_FALSE(io.enabled);
    EXPECT_EQ(io.brightness, -1);
}

TEST(IoStatesArray, DefaultsVisibleAndEnabledToTrue)
{
    auto events = receive(MSG_IO_STATES, json::array({{{"io_id", "arr_defaults"}}}));
    const auto& io = batchOf(events)->ioStates.at("arr_defaults");
    EXPECT_TRUE(io.visible);
    EXPECT_TRUE(io.enabled);
    EXPECT_TRUE(io.state.empty());
}

TEST(IoStatesArray, IdFallsBackToIdKeyAndIoIdWins)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"id", "arr_by_id"}, {"state", "1"}},
        {{"io_id", "arr_io_id"}, {"id", "arr_ignored"}, {"state", "2"}}}));

    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 2u);
    EXPECT_EQ(b->ioStates.at("arr_by_id").state, "1");
    EXPECT_EQ(b->ioStates.at("arr_io_id").state, "2");
    EXPECT_EQ(b->ioStates.count("arr_ignored"), 0u);
}

TEST(IoStatesArray, ElementWithoutIdIsSkipped)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"state", "x"}}, {{"io_id", "arr_kept"}, {"state", "y"}}}));
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 1u);
    EXPECT_EQ(b->ioStates.count("arr_kept"), 1u);
}

TEST(IoStatesArray, BooleanStateBecomesString)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "arr_on"}, {"state", true}}, {{"io_id", "arr_off"}, {"state", false}}}));
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.at("arr_on").state, "true");
    EXPECT_EQ(b->ioStates.at("arr_off").state, "false");
}

TEST(IoStatesArray, DuplicateIdKeepsLastElement)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "arr_dup"}, {"state", "first"}}, {{"io_id", "arr_dup"}, {"state", "last"}}}));
    EXPECT_EQ(batchOf(events)->ioStates.at("arr_dup").state, "last");
}

TEST(IoStatesArray, EmptyArrayStillDispatchesAnEmptyBatch)
{
    auto events = receive(MSG_IO_STATES, json::array());
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->ioStates.empty());
}

TEST(IoStatesArray, ReachesTheStore)
{
    receive(MSG_IO_STATES, json::array({{{"io_id", "arr_store"}, {"state", "7"}, {"name", "N"}}}));
    auto st = AppStore::getInstance().getStateCopy();
    ASSERT_EQ(st.ioStates.count("arr_store"), 1u);
    EXPECT_EQ(st.ioStates.at("arr_store").state, "7");
    EXPECT_EQ(st.ioStates.at("arr_store").name, "N");
}

TEST(IoStatesObject, StringAndBooleanShorthands)
{
    auto events = receive(MSG_IO_STATES, json{
        {"obj_str", "42"}, {"obj_true", true}, {"obj_false", false}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.at("obj_str").id, "obj_str");
    EXPECT_EQ(b->ioStates.at("obj_str").state, "42");
    EXPECT_EQ(b->ioStates.at("obj_true").state, "true");
    EXPECT_EQ(b->ioStates.at("obj_false").state, "false");
}

TEST(IoStatesObject, NestedObjectParsesAllFields)
{
    auto events = receive(MSG_IO_STATES, json{
        {"obj_full", {{"type", "WITemp"}, {"state", "21.5"}, {"gui_type", "temp"},
                      {"name", "Chambre"}, {"visible", false}, {"enabled", false}}}});
    const auto& io = batchOf(events)->ioStates.at("obj_full");
    EXPECT_EQ(io.id, "obj_full");
    EXPECT_EQ(io.type, "WITemp");
    EXPECT_EQ(io.state, "21.5");
    EXPECT_EQ(io.gui_type, "temp");
    EXPECT_EQ(io.name, "Chambre");
    EXPECT_FALSE(io.visible);
    EXPECT_FALSE(io.enabled);
}

TEST(IoStatesObject, InnerIdOverridesTheMapKeyOnTheStateOnly)
{
    auto events = receive(MSG_IO_STATES, json{{"obj_key", {{"id", "obj_inner"}, {"state", "s"}}}});
    auto* b = batchOf(events);
    ASSERT_EQ(b->ioStates.size(), 1u);
    ASSERT_EQ(b->ioStates.count("obj_key"), 1u);
    EXPECT_EQ(b->ioStates.at("obj_key").id, "obj_inner");
}

TEST(IoStatesObject, BooleanStateInsideObject)
{
    auto events = receive(MSG_IO_STATES, json{{"obj_inner_bool", {{"state", true}}}});
    EXPECT_EQ(batchOf(events)->ioStates.at("obj_inner_bool").state, "true");
}

TEST(IoStatesObject, EmptyObjectDispatchesAnEmptyBatch)
{
    auto events = receive(MSG_IO_STATES, json::object());
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->ioStates.empty());
}

TEST(IoStates, ScalarPayloadIsIgnored)
{
    EXPECT_EQ(countBatches(receive(MSG_IO_STATES, json("nope"))), 0);
    EXPECT_EQ(countBatches(receive(MSG_IO_STATES, json(5))), 0);
}

TEST(IoStates, MissingDataIsTreatedAsAnEmptyObject)
{
    auto events = receive(R"({"msg": "remote_ui_io_states"})");
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->ioStates.empty());
}

// ---- Known defects, pinned as they behave today. ----
// RUI-2 fixes these; the expectations below are then expected to flip.

// BUG: json::value(key, bool) throws type_error on a string, the catch-all
// swallows it and the whole batch is dropped, including the valid entries.
TEST(IoStatesKnownBugs, StringVisibleDropsTheWholeArrayBatch)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "bug_ok_1"}, {"state", "on"}},
        {{"io_id", "bug_str_visible"}, {"visible", "true"}},
        {{"io_id", "bug_ok_2"}, {"state", "on"}}}));

    EXPECT_EQ(countBatches(events), 0);
    EXPECT_FALSE(storeHas("bug_ok_1"));
    EXPECT_FALSE(storeHas("bug_str_visible"));
    EXPECT_FALSE(storeHas("bug_ok_2"));
}

TEST(IoStatesKnownBugs, StringEnabledDropsTheWholeArrayBatch)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "bug_en_ok"}, {"state", "on"}},
        {{"io_id", "bug_str_enabled"}, {"enabled", "false"}}}));
    EXPECT_EQ(countBatches(events), 0);
    EXPECT_FALSE(storeHas("bug_en_ok"));
}

TEST(IoStatesKnownBugs, StringVisibleDropsTheWholeObjectBatch)
{
    auto events = receive(MSG_IO_STATES, json{
        {"bug_obj_ok", {{"state", "on"}}},
        {"bug_obj_str_visible", {{"visible", "true"}}}});
    EXPECT_EQ(countBatches(events), 0);
    EXPECT_FALSE(storeHas("bug_obj_ok"));
}

// Same mechanism with other non-string/bool types: a numeric state is lost too.
TEST(IoStatesKnownBugs, NumericStateDropsTheWholeBatch)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "bug_num_ok"}, {"state", "on"}},
        {{"io_id", "bug_num_state"}, {"state", 21.5}}}));
    EXPECT_EQ(countBatches(events), 0);
    EXPECT_FALSE(storeHas("bug_num_ok"));
}

TEST(IoStatesKnownBugs, NonObjectArrayElementDropsTheWholeBatch)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "bug_elem_ok"}, {"state", "on"}}, "stray"}));
    EXPECT_EQ(countBatches(events), 0);
    EXPECT_FALSE(storeHas("bug_elem_ok"));
}

// BUG (RUI-2): a numeric or null value is not a recognised shorthand, so the entry is
// created with an empty state instead of the value (or being skipped).
TEST(IoStatesKnownBugs, NumericValueYieldsAnEntryWithEmptyState)
{
    auto events = receive(MSG_IO_STATES, json{{"obj_num", 12}, {"obj_null", nullptr}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 2u);
    EXPECT_TRUE(b->ioStates.at("obj_num").state.empty());
    EXPECT_TRUE(b->ioStates.at("obj_null").state.empty());
}

// BUG (RUI-2): the empty state then overwrites the one already stored, so a sensor
// sent as a bare number blanks its widget.
TEST(IoStatesKnownBugs, NumericValueClearsAnExistingStoredState)
{
    receive(MSG_IO_STATES, json{{"obj_clear", "on"}});
    ASSERT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("obj_clear").state, "on");

    receive(MSG_IO_STATES, json{{"obj_clear", 3}});
    EXPECT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("obj_clear").state, "");
}

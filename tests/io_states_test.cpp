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

// The key is what a widget subscribes to; an inner id must not move the entry.
TEST(IoStatesObject, TheMapKeyIsTheIdAndAnInnerIdIsIgnored)
{
    auto events = receive(MSG_IO_STATES, json{{"obj_key", {{"id", "obj_inner"}, {"state", "s"}}}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->ioStates.size(), 1u);
    ASSERT_EQ(b->ioStates.count("obj_key"), 1u);
    EXPECT_EQ(b->ioStates.at("obj_key").id, "obj_key");
    EXPECT_TRUE(storeHas("obj_key"));
    EXPECT_FALSE(storeHas("obj_inner"));
}

TEST(IoStatesObject, EmptyInnerIdDoesNotCreateAnEmptyKeyedIo)
{
    auto events = receive(MSG_IO_STATES, json{{"obj_empty_inner", {{"id", ""}, {"state", "s"}}}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->ioStates.size(), 1u);
    EXPECT_EQ(b->ioStates.at("obj_empty_inner").id, "obj_empty_inner");
    EXPECT_FALSE(storeHas(""));
}

TEST(IoStatesObject, EmptyKeyIsSkipped)
{
    auto events = receive(MSG_IO_STATES, json{{"", "x"}, {"obj_after_empty", "y"}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 1u);
    EXPECT_FALSE(storeHas(""));
}

TEST(IoStatesArray, EmptyIoIdWithNoIdIsSkipped)
{
    auto events = receive(MSG_IO_STATES, json::array({{{"io_id", ""}, {"state", "x"}}}));
    ASSERT_NE(batchOf(events), nullptr);
    EXPECT_TRUE(batchOf(events)->ioStates.empty());
    EXPECT_FALSE(storeHas(""));
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

TEST(IoStatesArray, StringFlagsAreAcceptedAndDoNotLoseTheBatch)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "tol_ok_1"}, {"state", "on"}},
        {{"io_id", "tol_str_visible"}, {"visible", "true"}, {"enabled", "false"}},
        {{"io_id", "tol_str_hidden"}, {"visible", "false"}, {"enabled", "true"}},
        {{"io_id", "tol_ok_2"}, {"state", "on"}}}));

    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 4u);
    EXPECT_TRUE(b->ioStates.at("tol_str_visible").visible);
    EXPECT_FALSE(b->ioStates.at("tol_str_visible").enabled);
    EXPECT_FALSE(b->ioStates.at("tol_str_hidden").visible);
    EXPECT_TRUE(b->ioStates.at("tol_str_hidden").enabled);
    EXPECT_TRUE(storeHas("tol_ok_1"));
    EXPECT_TRUE(storeHas("tol_ok_2"));
}

TEST(IoStatesArray, FlagOfAnUnexpectedTypeFallsBackToTrue)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "tol_flag_obj"}, {"visible", json::object()}, {"enabled", nullptr}}}));
    ASSERT_NE(batchOf(events), nullptr);
    const auto& io = batchOf(events)->ioStates.at("tol_flag_obj");
    EXPECT_TRUE(io.visible);
    EXPECT_TRUE(io.enabled);
}

TEST(IoStatesObject, StringFlagsAreAcceptedAndDoNotLoseTheBatch)
{
    auto events = receive(MSG_IO_STATES, json{
        {"tol_obj_ok", {{"state", "on"}}},
        {"tol_obj_str_visible", {{"visible", "false"}, {"enabled", "true"}}}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 2u);
    EXPECT_FALSE(b->ioStates.at("tol_obj_str_visible").visible);
    EXPECT_TRUE(b->ioStates.at("tol_obj_str_visible").enabled);
}

TEST(IoStatesArray, NumericStateIsConvertedToAString)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "tol_num_ok"}, {"state", "on"}},
        {{"io_id", "tol_num_float"}, {"state", 21.5}},
        {{"io_id", "tol_num_int"}, {"state", 7}}}));
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 3u);
    EXPECT_EQ(b->ioStates.at("tol_num_float").state, "21.5");
    EXPECT_EQ(b->ioStates.at("tol_num_int").state, "7");
}

TEST(IoStatesArray, DescriptiveFieldsOfTheWrongTypeDoNotLoseTheEntry)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "tol_wrong"}, {"state", "on"}, {"name", json::object()}, {"type", json::array()},
         {"gui_type", 3}}}));
    ASSERT_NE(batchOf(events), nullptr);
    const auto& io = batchOf(events)->ioStates.at("tol_wrong");
    EXPECT_EQ(io.state, "on");
    EXPECT_TRUE(io.name.empty());
    EXPECT_TRUE(io.type.empty());
    EXPECT_EQ(io.gui_type, "3");
}

TEST(IoStatesArray, NonObjectElementIsSkippedAndTheRestKept)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", "tol_elem_ok"}, {"state", "on"}}, "stray", 4, nullptr,
        {{"io_id", "tol_elem_ok2"}, {"state", "off"}}}));
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 2u);
    EXPECT_TRUE(storeHas("tol_elem_ok"));
    EXPECT_TRUE(storeHas("tol_elem_ok2"));
}

TEST(IoStatesArray, NonStringIdIsSkippedAndTheRestKept)
{
    auto events = receive(MSG_IO_STATES, json::array({
        {{"io_id", 5}, {"state", "x"}}, {{"io_id", "tol_id_ok"}, {"state", "y"}}}));
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 1u);
    EXPECT_EQ(b->ioStates.count("tol_id_ok"), 1u);
}

TEST(IoStatesObject, NumericValueIsConvertedToTheState)
{
    auto events = receive(MSG_IO_STATES, json{{"tol_obj_int", 12}, {"tol_obj_float", 3.5}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.at("tol_obj_int").state, "12");
    EXPECT_EQ(b->ioStates.at("tol_obj_float").state, "3.5");
}

TEST(IoStatesObject, NullAndContainerValuesAreSkipped)
{
    auto events = receive(MSG_IO_STATES, json{
        {"tol_obj_null", nullptr}, {"tol_obj_list", json::array({1})}, {"tol_obj_kept", "on"}});
    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ioStates.size(), 1u);
    EXPECT_EQ(b->ioStates.count("tol_obj_kept"), 1u);
}

TEST(IoStatesObject, NumericValueUpdatesAnExistingStoredState)
{
    receive(MSG_IO_STATES, json{{"tol_obj_clear", "on"}});
    ASSERT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("tol_obj_clear").state, "on");

    receive(MSG_IO_STATES, json{{"tol_obj_clear", 3}});
    EXPECT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("tol_obj_clear").state, "3");
}

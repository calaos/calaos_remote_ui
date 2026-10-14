#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_store.h"

using nlohmann::json;

namespace
{
std::vector<AppEvent> ioChanged(const json& eventData)
{
    return receive(CalaosProtocol::MSG_EVENT, {{"type_str", "io_changed"}, {"data", eventData}});
}

const IoStateReceivedData* single(const std::vector<AppEvent>& events)
{
    return firstData<IoStateReceivedData>(events, AppEventType::IoStateReceived);
}

std::string changedState(const json& eventData)
{
    auto events = ioChanged(eventData);
    auto* d = single(events);
    return d ? d->ioState.state : "<event lost>";
}
}

TEST(IoChanged, DispatchesIdAndStateOnly)
{
    auto events = ioChanged({{"id", "ev_light"}, {"state", "true"}, {"name", "ignored"},
                             {"type", "ignored"}, {"gui_type", "ignored"}, {"visible", "ignored"}});
    auto* d = single(events);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->ioState.id, "ev_light");
    EXPECT_EQ(d->ioState.state, "true");
    EXPECT_TRUE(d->ioState.name.empty());
    EXPECT_TRUE(d->ioState.type.empty());
    EXPECT_TRUE(d->ioState.gui_type.empty());
    EXPECT_TRUE(d->ioState.visible);
    EXPECT_TRUE(d->ioState.enabled);
}

TEST(IoChanged, MissingStateDispatchesAnEmptyState)
{
    auto events = ioChanged({{"id", "ev_nostate"}});
    auto* d = single(events);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->ioState.state.empty());
}

TEST(IoChanged, MissingIdIsIgnored)
{
    EXPECT_EQ(single(ioChanged({{"state", "true"}})), nullptr);
    EXPECT_EQ(single(ioChanged({{"id", ""}, {"state", "true"}})), nullptr);
}

TEST(IoChanged, DataThatIsNotAnObjectIsIgnored)
{
    EXPECT_EQ(single(ioChanged(json::array({1, 2}))), nullptr);
    EXPECT_EQ(single(ioChanged("x")), nullptr);
}

TEST(IoChanged, MissingInnerDataIsIgnored)
{
    EXPECT_EQ(single(receive(CalaosProtocol::MSG_EVENT, {{"type_str", "io_changed"}})), nullptr);
}

TEST(IoChanged, OtherEventTypesAreIgnored)
{
    auto events = receive(CalaosProtocol::MSG_EVENT,
                          {{"type_str", "io_added"}, {"data", {{"id", "ev_other"}, {"state", "1"}}}});
    EXPECT_EQ(single(events), nullptr);
}

TEST(IoChanged, UpdatesTheStoredState)
{
    ioChanged({{"id", "ev_store"}, {"state", "a"}});
    EXPECT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("ev_store").state, "a");
    ioChanged({{"id", "ev_store"}, {"state", "b"}});
    EXPECT_EQ(AppStore::getInstance().getStateCopy().ioStates.at("ev_store").state, "b");
}

TEST(IoChanged, BooleanStateBecomesAString)
{
    EXPECT_EQ(changedState({{"id", "ev_bool_t"}, {"state", true}}), "true");
    EXPECT_EQ(changedState({{"id", "ev_bool_f"}, {"state", false}}), "false");
}

TEST(IoChanged, NumericStateBecomesAString)
{
    EXPECT_EQ(changedState({{"id", "ev_num_f"}, {"state", 21.5}}), "21.5");
    EXPECT_EQ(changedState({{"id", "ev_num_i"}, {"state", 7}}), "7");
}

TEST(IoChanged, NumericStateReachesTheStore)
{
    ioChanged({{"id", "ev_num_store"}, {"state", 18}});
    const auto st = AppStore::getInstance().getStateCopy();
    ASSERT_EQ(st.ioStates.count("ev_num_store"), 1u);
    EXPECT_EQ(st.ioStates.at("ev_num_store").state, "18");
}

TEST(IoChanged, StateOfAnUnsupportedTypeIsEmptyButTheEventIsKept)
{
    auto events = ioChanged({{"id", "ev_state_obj"}, {"state", json::object()}});
    auto* d = single(events);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->ioState.state.empty());
    auto nullEvents = ioChanged({{"id", "ev_state_null"}, {"state", nullptr}});
    EXPECT_NE(single(nullEvents), nullptr);
}

TEST(IoChanged, NonStringIdIsIgnored)
{
    EXPECT_EQ(single(ioChanged({{"id", 12}, {"state", "x"}})), nullptr);
}

// Contrast with remote_ui_io_states and io_state, which do convert booleans.
TEST(IoState, SingleUpdateConvertsBooleanState)
{
    auto events = receive(CalaosProtocol::MSG_IO_STATE, {{"io_id", "single_bool"}, {"state", true}});
    auto* d = single(events);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->ioState.id, "single_bool");
    EXPECT_EQ(d->ioState.state, "true");
}

TEST(IoState, SingleUpdateWithoutIoIdIsIgnored)
{
    EXPECT_EQ(single(receive(CalaosProtocol::MSG_IO_STATE, {{"id", "x"}, {"state", "1"}})), nullptr);
}

TEST(IoState, UnknownMessageTypeAndMissingMsgAreIgnored)
{
    EXPECT_TRUE(receive(R"({"msg": "nope", "data": {}})").empty());
    EXPECT_TRUE(receive(R"({"data": {}})").empty());
    EXPECT_TRUE(receive("not json").empty());
}

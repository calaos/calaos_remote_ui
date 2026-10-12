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
    auto* d = single(ioChanged({{"id", "ev_nostate"}}));
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

// ---- Known defects, pinned as they behave today. ----

// BUG: only strings are accepted; json::value(key, "") throws on any other
// type and the event is lost without a trace beyond the error log.
TEST(IoChangedKnownBugs, BooleanStateLosesTheEvent)
{
    EXPECT_EQ(single(ioChanged({{"id", "ev_bug_bool"}, {"state", true}})), nullptr);
}

TEST(IoChangedKnownBugs, NumericStateLosesTheEvent)
{
    EXPECT_EQ(single(ioChanged({{"id", "ev_bug_num"}, {"state", 21.5}})), nullptr);
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

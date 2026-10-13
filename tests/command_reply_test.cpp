#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_store.h"

using nlohmann::json;
using M = CalaosWebSocketManager;

namespace
{
json reply(const json& msgId, const json& success)
{
    json j = {{"msg", "set_state"}, {"data", {{"success", success}}}};
    if (!msgId.is_null())
        j["msg_id"] = msgId;
    return j;
}

uint32_t refusalsOf(const std::string& id)
{
    auto s = AppStore::getInstance().getStateCopy();
    auto it = s.ioCommandRefusals.find(id);
    return it == s.ioCommandRefusals.end() ? 0 : it->second;
}

const IoCommandRefusedData* refusalOf(const std::vector<AppEvent>& events)
{
    return firstData<IoCommandRefusedData>(events, AppEventType::IoCommandRefused);
}
}

TEST(DeviceCommandReply, CommandsAskForAVerdictNamingTheIo)
{
    auto json_cmd = json::parse(*M::buildDeviceCommandMessage("salon_ac", {{"target_temp", 21}}));
    EXPECT_EQ(json_cmd["msg_id"], "ui_cmd:salon_ac");

    auto mode = json::parse(*M::buildDeviceModeMessage("salon_ac", "heat"));
    EXPECT_EQ(mode["msg"], "set_state");
    EXPECT_EQ(mode["msg_id"], "ui_cmd:salon_ac");
    EXPECT_EQ(mode["data"]["id"], "salon_ac");
    EXPECT_EQ(mode["data"]["value"], "heat");
}

TEST(DeviceCommandReply, OtherWidgetsStillSendNoMsgId)
{
    auto msg = json::parse(M::buildSetStateMessage("light", "true"));
    EXPECT_FALSE(msg.contains("msg_id"));
}

TEST(DeviceCommandReply, APlainModeIsNeverEmptyNorJson)
{
    EXPECT_FALSE(M::buildDeviceModeMessage("salon_ac", "").has_value());
    EXPECT_FALSE(M::buildDeviceModeMessage("salon_ac", "{\"mode\":\"off\"}").has_value());
    EXPECT_FALSE(M::buildDeviceModeMessage("", "off").has_value());

    CalaosWebSocketManager mgr;
    EXPECT_FALSE(mgr.sendDeviceMode("salon_ac", "off"));
}

TEST(DeviceCommandReply, ARefusalReachesTheStoreForItsIo)
{
    const uint32_t before = refusalsOf("reply_ac");
    auto events = receive(reply("ui_cmd:reply_ac", "false").dump());

    ASSERT_NE(refusalOf(events), nullptr);
    EXPECT_EQ(refusalOf(events)->ioId, "reply_ac");
    EXPECT_EQ(refusalsOf("reply_ac"), before + 1);

    receive(reply("ui_cmd:reply_ac", false).dump());
    EXPECT_EQ(refusalsOf("reply_ac"), before + 2);
}

TEST(DeviceCommandReply, OnlyARefusedDeviceCommandCounts)
{
    EXPECT_EQ(refusalOf(receive(reply("ui_cmd:reply_ok", "true").dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(reply("ui_cmd:reply_ok", true).dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(reply("other:reply_ok", "false").dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(reply("ui_cmd:", "false").dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(reply(nullptr, "false").dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(reply(42, "false").dump())), nullptr);
    EXPECT_EQ(refusalOf(receive(json{{"msg", "set_state"}, {"msg_id", "ui_cmd:reply_ok"}}.dump())), nullptr);
    EXPECT_EQ(refusalsOf("reply_ok"), 0u);
}

#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_store.h"
#include "protocol_json.h"

using nlohmann::json;
using CalaosProtocol::DeviceInfo;
using CalaosProtocol::IoState;

namespace
{
// Spec 5.3, verbatim shape.
json climateDevice()
{
    return json::parse(R"({
        "family": "climate", "impl": "proxy",
        "capabilities": {
            "modes": ["off", "heat", "cool", "auto"],
            "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C"},
            "target_temp_range": true,
            "fan_modes": ["auto", "low", "high"],
            "reports_action": true,
            "remote_temp": true
        },
        "state": {"mode": "cool", "current_temp": 23.4, "target_temp": 21.0, "action": "cooling",
                  "fan_mode": "auto"},
        "ios": {"mode": "salon_ac_mode", "target_temp": "salon_ac_target"}
    })");
}

json item(const std::string& id, const json& device)
{
    return {{"io_id", id}, {"gui_type", "climate"}, {"state", "cool"}, {"device", device}};
}

const IoStatesReceivedData* batchOf(const std::vector<AppEvent>& events)
{
    return firstData<IoStatesReceivedData>(events, AppEventType::IoStatesReceived);
}

const IoStateReceivedData* singleOf(const std::vector<AppEvent>& events)
{
    return firstData<IoStateReceivedData>(events, AppEventType::IoStateReceived);
}

std::vector<AppEvent> ioChanged(const json& eventData)
{
    return receive(CalaosProtocol::MSG_EVENT, {{"type_str", "io_changed"}, {"data", eventData}});
}

IoState stored(const std::string& id)
{
    return AppStore::getInstance().getStateCopy().ioStates.at(id);
}

void expectClimate(const std::optional<DeviceInfo>& d)
{
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->family, "climate");
    EXPECT_EQ(d->impl, "proxy");
    EXPECT_EQ(json::parse(d->capabilities), climateDevice()["capabilities"]);
    EXPECT_EQ(json::parse(d->state), climateDevice()["state"]);
    EXPECT_EQ(json::parse(d->ios), climateDevice()["ios"]);
}

struct Subscription
{
    int calls = 0;
    SubscriptionId id;

    Subscription() { id = AppStore::getInstance().subscribe([this](const AppState&) { calls++; }); }
    ~Subscription() { AppStore::getInstance().unsubscribe(id); }
};
}

TEST(DeviceParsing, ArrayFormCarriesTheDevice)
{
    auto events = receive(CalaosProtocol::MSG_IO_STATES, json::array({item("dev_arr", climateDevice())}));
    ASSERT_NE(batchOf(events), nullptr);
    const auto& io = batchOf(events)->ioStates.at("dev_arr");
    EXPECT_EQ(io.gui_type, "climate");
    EXPECT_EQ(io.state, "cool");
    expectClimate(io.device);
}

TEST(DeviceParsing, ObjectFormCarriesTheDevice)
{
    auto events = receive(CalaosProtocol::MSG_IO_STATES, json{
        {"dev_obj", {{"gui_type", "climate"}, {"state", "cool"}, {"device", climateDevice()}}}});
    ASSERT_NE(batchOf(events), nullptr);
    expectClimate(batchOf(events)->ioStates.at("dev_obj").device);
}

TEST(DeviceParsing, IoChangedCarriesTheDeviceAndIsPartial)
{
    auto events = ioChanged({{"id", "dev_ev"}, {"state", "heat"}, {"device", climateDevice()}});
    ASSERT_NE(singleOf(events), nullptr);
    const auto& io = singleOf(events)->ioState;
    EXPECT_EQ(io.state, "heat");
    EXPECT_TRUE(io.partial);
    expectClimate(io.device);
}

TEST(DeviceParsing, SingleIoStateMessageCarriesTheDevice)
{
    auto events = receive(CalaosProtocol::MSG_IO_STATE,
                          {{"io_id", "dev_single"}, {"state", "cool"}, {"device", climateDevice()}});
    ASSERT_NE(singleOf(events), nullptr);
    expectClimate(singleOf(events)->ioState.device);
}

TEST(DeviceParsing, ItemWithoutDeviceHasNone)
{
    auto events = receive(CalaosProtocol::MSG_IO_STATES, json::array({{{"io_id", "dev_none"}, {"state", "on"}}}));
    EXPECT_FALSE(batchOf(events)->ioStates.at("dev_none").device.has_value());
    EXPECT_FALSE(singleOf(ioChanged({{"id", "dev_none_ev"}, {"state", "on"}}))->ioState.device.has_value());
}

TEST(DeviceParsing, NonAsciiUnitsSurvive)
{
    auto events = ioChanged({{"id", "dev_utf8"}, {"device", climateDevice()}});
    const auto& d = singleOf(events)->ioState.device;
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(json::parse(d->capabilities)["target_temp"]["unit"], "°C");
}

TEST(DeviceParsing, MalformedDeviceIsIgnoredAndTheBatchIsKept)
{
    json noFamily = climateDevice();
    noFamily.erase("family");

    auto events = receive(CalaosProtocol::MSG_IO_STATES, json::array({
        item("dev_bad_str", "oops"),
        item("dev_bad_arr", json::array({1})),
        item("dev_bad_null", nullptr),
        item("dev_bad_nofamily", noFamily),
        item("dev_good", climateDevice())}));

    auto* b = batchOf(events);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->ioStates.size(), 5u);
    for (const char* id : {"dev_bad_str", "dev_bad_arr", "dev_bad_null", "dev_bad_nofamily"})
    {
        SCOPED_TRACE(id);
        EXPECT_FALSE(b->ioStates.at(id).device.has_value());
        EXPECT_EQ(b->ioStates.at(id).state, "cool");
    }
    expectClimate(b->ioStates.at("dev_good").device);
}

TEST(DeviceParsing, MalformedDeviceInIoChangedStillUpdatesTheState)
{
    auto events = ioChanged({{"id", "dev_ev_bad"}, {"state", "heat"}, {"device", 12}});
    ASSERT_NE(singleOf(events), nullptr);
    EXPECT_EQ(singleOf(events)->ioState.state, "heat");
    EXPECT_FALSE(singleOf(events)->ioState.device.has_value());
}

TEST(DeviceParsing, MembersOfTheWrongTypeBecomeEmptyObjects)
{
    json d = {{"family", "climate"}, {"capabilities", "x"}, {"state", json::array()}, {"ios", 4}};
    auto events = ioChanged({{"id", "dev_members"}, {"device", d}});
    const auto& got = singleOf(events)->ioState.device;
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->family, "climate");
    EXPECT_TRUE(got->impl.empty());
    EXPECT_EQ(got->capabilities, "{}");
    EXPECT_EQ(got->state, "{}");
    EXPECT_EQ(got->ios, "{}");
}

TEST(DeviceParsing, KeyOrderDoesNotMatter)
{
    auto a = singleOf(ioChanged({{"id", "dev_order"}, {"device",
        json::parse(R"({"family":"climate","state":{"a":1,"b":2}})")}}))->ioState.device;
    auto b = singleOf(ioChanged({{"id", "dev_order"}, {"device",
        json::parse(R"({"state":{"b":2,"a":1},"family":"climate"})")}}))->ioState.device;
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, *b);
}

TEST(DeviceStore, DeviceIsStoredAndKeptWhenALaterUpdateHasNone)
{
    ioChanged({{"id", "dev_keep"}, {"state", "cool"}, {"device", climateDevice()}});
    expectClimate(stored("dev_keep").device);

    receive(CalaosProtocol::MSG_IO_STATE, {{"io_id", "dev_keep"}, {"state", "heat"}});
    EXPECT_EQ(stored("dev_keep").state, "heat");
    expectClimate(stored("dev_keep").device);
}

TEST(DeviceStore, NewDeviceReplacesTheStoredOne)
{
    ioChanged({{"id", "dev_replace"}, {"device", climateDevice()}});
    json next = climateDevice();
    next["state"]["target_temp"] = 19.5;
    ioChanged({{"id", "dev_replace"}, {"device", next}});

    EXPECT_EQ(json::parse(stored("dev_replace").device->state)["target_temp"], 19.5);
}

// Driven straight into the store: through the dispatcher every settle() adds a
// sentinel event that the store also announces.
void storeReceive(const std::string& id, const std::string& state, const json& device)
{
    IoState io;
    io.id = id;
    io.state = state;
    io.partial = true;
    io.device = CalaosProtocol::wire::deviceField({{"device", device}});
    AppStore::getInstance().handleEvent(AppEvent(AppEventType::IoStateReceived, IoStateReceivedData{io}));
}

TEST(DeviceStore, OnlyARealDeviceChangeNotifies)
{
    storeReceive("dev_notify", "cool", climateDevice());
    Subscription sub;

    storeReceive("dev_notify", "cool", climateDevice());
    EXPECT_EQ(sub.calls, 0);

    json next = climateDevice();
    next["state"]["current_temp"] = 23.5;
    storeReceive("dev_notify", "cool", next);
    EXPECT_EQ(sub.calls, 1);

    json caps = next;
    caps["capabilities"]["fan_modes"] = json::array({"auto"});
    storeReceive("dev_notify", "cool", caps);
    EXPECT_EQ(sub.calls, 2);
}

TEST(DeviceStore, FirstDeviceOnAnExistingIoNotifies)
{
    // Unique per pass: the store keeps the device from a previous --gtest_repeat pass.
    static int pass = 0;
    const std::string id = "dev_first_" + std::to_string(pass++);

    IoState plain;
    plain.id = id;
    plain.state = "cool";
    plain.partial = true;
    AppStore::getInstance().handleEvent(AppEvent(AppEventType::IoStateReceived, IoStateReceivedData{plain}));
    Subscription sub;

    storeReceive(id, "cool", climateDevice());
    EXPECT_EQ(sub.calls, 1);
}

TEST(DeviceStore, BatchMergesTheDeviceToo)
{
    receive(CalaosProtocol::MSG_IO_STATES, json::array({item("dev_batch", climateDevice())}));
    expectClimate(stored("dev_batch").device);

    receive(CalaosProtocol::MSG_IO_STATES, json::array({{{"io_id", "dev_batch"}, {"state", "off"}}}));
    EXPECT_EQ(stored("dev_batch").state, "off");
    expectClimate(stored("dev_batch").device);
}

TEST(PartialUpdates, IoChangedDoesNotResetTheVisibilityFromTheConfig)
{
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({
        {{"id", "part_hidden"}, {"visible", "false"}, {"rw", "false"}}})}});
    ASSERT_FALSE(stored("part_hidden").visible);

    ioChanged({{"id", "part_hidden"}, {"state", "on"}});
    receive(CalaosProtocol::MSG_IO_STATE, {{"io_id", "part_hidden"}, {"state", "off"}});
    receive(CalaosProtocol::MSG_IO_STATES, json{{"part_hidden", "on"}});

    EXPECT_EQ(stored("part_hidden").state, "on");
    EXPECT_FALSE(stored("part_hidden").visible);
    EXPECT_FALSE(stored("part_hidden").enabled);
}

TEST(PartialUpdates, BatchEntriesWithoutFlagsKeepTheStoredOnes)
{
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({
        {{"id", "part_array"}, {"visible", "false"}}, {{"id", "part_object"}, {"rw", "false"}}})}});

    receive(CalaosProtocol::MSG_IO_STATES, json::array({{{"io_id", "part_array"}, {"state", "on"}}}));
    receive(CalaosProtocol::MSG_IO_STATES, json{{"part_object", {{"state", "on"}}}});

    EXPECT_EQ(stored("part_array").state, "on");
    EXPECT_FALSE(stored("part_array").visible);
    EXPECT_EQ(stored("part_object").state, "on");
    EXPECT_FALSE(stored("part_object").enabled);
}

TEST(PartialUpdates, ABatchEntryThatSaysVisibleDoesUpdateIt)
{
    receive(CalaosProtocol::MSG_CONFIG_UPDATE, {{"io_items", json::array({
        {{"id", "part_shown"}, {"visible", "false"}}})}});
    receive(CalaosProtocol::MSG_IO_STATES, json::array({
        {{"io_id", "part_shown"}, {"visible", "true"}}}));
    EXPECT_TRUE(stored("part_shown").visible);
}

TEST(IoStateEquality, DeviceAndEveryFieldCount)
{
    IoState a;
    a.id = "eq";
    IoState b = a;
    EXPECT_EQ(a, b);

    b.device = DeviceInfo{};
    EXPECT_NE(a, b);

    IoState c = b;
    c.device->state = "{\"mode\":\"off\"}";
    EXPECT_NE(b, c);

    IoState d = a;
    d.brightness = 5;
    EXPECT_NE(a, d);

    IoState e = a;
    e.gui_type = "x";
    EXPECT_NE(a, e);
}

TEST(IoStateEquality, PartialIsAnUpdateMarkerNotState)
{
    IoState a;
    IoState b;
    b.partial = true;
    EXPECT_EQ(a, b);
}

TEST(DeviceCommand, SetStateCarriesAPlainModeAsIs)
{
    auto msg = json::parse(CalaosWebSocketManager::buildSetStateMessage("salon_ac", "cool"));
    EXPECT_EQ(msg["msg"], "set_state");
    EXPECT_EQ(msg["data"]["id"], "salon_ac");
    EXPECT_EQ(msg["data"]["value"], "cool");
}

TEST(DeviceCommand, JsonCommandTravelsAsAStringValue)
{
    auto message = CalaosWebSocketManager::buildDeviceCommandMessage(
        "salon_ac", {{"target_temp", 21.5}, {"mode", "heat"}});
    ASSERT_TRUE(message.has_value());

    auto msg = json::parse(*message);
    EXPECT_EQ(msg["msg"], "set_state");
    EXPECT_EQ(msg["data"]["id"], "salon_ac");
    ASSERT_TRUE(msg["data"]["value"].is_string());
    EXPECT_EQ(json::parse(msg["data"]["value"].get<std::string>()),
              (json{{"target_temp", 21.5}, {"mode", "heat"}}));
}

TEST(DeviceCommand, OnlyANonEmptyObjectForAnIdIsAPartialUpdate)
{
    using M = CalaosWebSocketManager;
    EXPECT_FALSE(M::buildDeviceCommandMessage("salon_ac", json::object()).has_value());
    EXPECT_FALSE(M::buildDeviceCommandMessage("salon_ac", json::array({1})).has_value());
    EXPECT_FALSE(M::buildDeviceCommandMessage("salon_ac", "cool").has_value());
    EXPECT_FALSE(M::buildDeviceCommandMessage("salon_ac", nullptr).has_value());
    EXPECT_FALSE(M::buildDeviceCommandMessage("", {{"mode", "off"}}).has_value());
}

TEST(DeviceCommand, NothingIsSentWhileDisconnected)
{
    CalaosWebSocketManager mgr;
    EXPECT_FALSE(mgr.sendDeviceCommand("salon_ac", {{"mode", "off"}}));
    EXPECT_FALSE(mgr.setIoState("salon_ac", "off"));
}

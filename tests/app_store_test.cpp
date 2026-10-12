#include <gtest/gtest.h>
#include "app_store.h"
#include "support/fake_hal.h"
#include <vector>

using CalaosProtocol::IoState;

namespace
{
// AppStore is a process-wide singleton without a reset, so every test works on
// its own io ids and only asserts on those.
AppStore& store() { return AppStore::getInstance(); }

// Unique per call so --gtest_repeat does not meet ids stored by a previous pass.
std::string uid(const std::string& prefix)
{
    static int counter = 0;
    return prefix + "_" + std::to_string(counter++);
}

IoState io(const std::string& id, const std::string& state = "",
           const std::string& name = "", const std::string& type = "",
           const std::string& guiType = "")
{
    IoState s;
    s.id = id;
    s.state = state;
    s.name = name;
    s.type = type;
    s.gui_type = guiType;
    return s;
}

void receiveOne(const IoState& s)
{
    store().handleEvent(AppEvent(AppEventType::IoStateReceived, IoStateReceivedData{s}));
}

void receiveBatch(const std::vector<IoState>& list)
{
    IoStatesReceivedData d;
    for (const auto& s : list)
        d.ioStates[s.id] = s;
    store().handleEvent(AppEvent(AppEventType::IoStatesReceived, d));
}

IoState stored(const AppState& st, const std::string& id)
{
    return st.ioStates.at(id);
}

struct Subscription
{
    int calls = 0;
    SubscriptionId id;

    Subscription()
    {
        id = store().subscribe([this](const AppState&) { calls++; });
    }
    ~Subscription() { store().unsubscribe(id); }
};
}

TEST(AppStoreIo, NewIoIsInsertedWithAllFields)
{
    const std::string st_new = uid("st_new");
    IoState s = io(st_new, "on", "Lampe", "WODigital", "light");
    s.visible = false;
    s.enabled = false;
    s.brightness = 40;
    receiveOne(s);

    const auto got = stored(store().getStateCopy(), st_new);
    EXPECT_EQ(got.state, "on");
    EXPECT_EQ(got.name, "Lampe");
    EXPECT_EQ(got.type, "WODigital");
    EXPECT_EQ(got.gui_type, "light");
    EXPECT_FALSE(got.visible);
    EXPECT_FALSE(got.enabled);
    EXPECT_EQ(got.brightness, 40);
}

TEST(AppStoreIo, StateAlwaysOverwritesEvenWithAnEmptyString)
{
    const std::string st_over = uid("st_over");
    receiveOne(io(st_over, "on"));
    receiveOne(io(st_over, ""));
    EXPECT_EQ(stored(store().getStateCopy(), st_over).state, "");
}

TEST(AppStoreIo, EmptyDescriptiveFieldsDoNotEraseExistingOnes)
{
    const std::string st_keep = uid("st_keep");
    receiveOne(io(st_keep, "a", "Name", "Type", "gui"));
    receiveOne(io(st_keep, "b"));

    const auto got = stored(store().getStateCopy(), st_keep);
    EXPECT_EQ(got.state, "b");
    EXPECT_EQ(got.name, "Name");
    EXPECT_EQ(got.type, "Type");
    EXPECT_EQ(got.gui_type, "gui");
}

TEST(AppStoreIo, NonEmptyDescriptiveFieldsReplaceExistingOnes)
{
    const std::string st_repl = uid("st_repl");
    receiveOne(io(st_repl, "a", "Old", "T1", "g1"));
    receiveOne(io(st_repl, "a", "New", "T2", "g2"));

    const auto got = stored(store().getStateCopy(), st_repl);
    EXPECT_EQ(got.name, "New");
    EXPECT_EQ(got.type, "T2");
    EXPECT_EQ(got.gui_type, "g2");
}

TEST(AppStoreIo, VisibleEnabledAndBrightnessAreMergedIntoAnExistingIo)
{
    const std::string st_flags = uid("st_flags");
    receiveOne(io(st_flags, "a"));

    IoState update = io(st_flags, "a");
    update.visible = false;
    update.enabled = false;
    update.brightness = 70;
    receiveOne(update);

    const auto got = stored(store().getStateCopy(), st_flags);
    EXPECT_FALSE(got.visible);
    EXPECT_FALSE(got.enabled);
    EXPECT_EQ(got.brightness, 70);
}

TEST(AppStoreIo, UnsetBrightnessDoesNotEraseAStoredOne)
{
    const std::string st_bri = uid("st_bri");
    IoState first = io(st_bri, "a");
    first.brightness = 40;
    receiveOne(first);

    receiveOne(io(st_bri, "b"));
    EXPECT_EQ(stored(store().getStateCopy(), st_bri).brightness, 40);
}

TEST(AppStoreIo, SingleUpdateNotifiesOnlyWhenSomethingChanged)
{
    const std::string st_notify = uid("st_notify");
    receiveOne(io(st_notify, "a"));
    Subscription sub;

    receiveOne(io(st_notify, "a"));
    EXPECT_EQ(sub.calls, 0);

    receiveOne(io(st_notify, "b"));
    EXPECT_EQ(sub.calls, 1);
}

TEST(AppStoreIo, SingleUpdateNotifiesOnAnyFieldChange)
{
    struct Case
    {
        const char* what;
        void (*mutate)(IoState&);
    };
    const Case cases[] = {
        {"name", [](IoState& s) { s.name = "Renamed"; }},
        {"type", [](IoState& s) { s.type = "Other"; }},
        {"gui_type", [](IoState& s) { s.gui_type = "other"; }},
        {"visible", [](IoState& s) { s.visible = false; }},
        {"enabled", [](IoState& s) { s.enabled = false; }},
        {"brightness", [](IoState& s) { s.brightness = 55; }},
    };

    for (const auto& c : cases)
    {
        SCOPED_TRACE(c.what);
        const std::string id = uid("st_any");
        receiveOne(io(id, "a", "Name", "Type", "gui"));
        Subscription sub;

        IoState update = io(id, "a", "Name", "Type", "gui");
        c.mutate(update);
        receiveOne(update);

        EXPECT_EQ(sub.calls, 1);
    }
}

TEST(AppStoreIo, SingleUpdateThatOnlyRenamesIsStoredAndNotified)
{
    const std::string st_rename = uid("st_rename");
    receiveOne(io(st_rename, "a", "Before"));
    Subscription sub;

    receiveOne(io(st_rename, "a", "After"));

    EXPECT_EQ(sub.calls, 1);
    EXPECT_EQ(stored(store().getStateCopy(), st_rename).name, "After");
}

TEST(AppStoreIo, NewIoNotifiesEvenWithAnEmptyState)
{
    const std::string st_new_empty = uid("st_new_empty");
    Subscription sub;
    receiveOne(io(st_new_empty, ""));
    EXPECT_EQ(sub.calls, 1);
}

TEST(AppStoreIo, BatchMergesLikeSingleUpdates)
{
    const std::string st_b1 = uid("st_b1");
    const std::string st_b2 = uid("st_b2");
    receiveOne(io(st_b1, "a", "Name", "Type", "gui"));
    receiveBatch({io(st_b1, "z"), io(st_b2, "y", "Two")});

    auto st = store().getStateCopy();
    EXPECT_EQ(stored(st, st_b1).state, "z");
    EXPECT_EQ(stored(st, st_b1).name, "Name");
    EXPECT_EQ(stored(st, st_b2).state, "y");
    EXPECT_EQ(stored(st, st_b2).name, "Two");
}

TEST(AppStoreIo, BatchNotifiesOnceEvenWhenNothingChanged)
{
    const std::string st_bn = uid("st_bn");
    receiveBatch({io(st_bn, "a")});
    Subscription sub;

    receiveBatch({io(st_bn, "a")});
    EXPECT_EQ(sub.calls, 1);
}

TEST(AppStoreIo, BatchMergesVisibleEnabledAndBrightness)
{
    const std::string st_bflags = uid("st_bflags");
    receiveBatch({io(st_bflags, "a")});

    IoState update = io(st_bflags, "a");
    update.visible = false;
    update.enabled = false;
    update.brightness = 30;
    receiveBatch({update});

    const auto got = stored(store().getStateCopy(), st_bflags);
    EXPECT_FALSE(got.visible);
    EXPECT_FALSE(got.enabled);
    EXPECT_EQ(got.brightness, 30);
}

TEST(AppStoreIo, EmptyBatchStillNotifies)
{
    Subscription sub;
    receiveBatch({});
    EXPECT_EQ(sub.calls, 1);
}

TEST(AppStoreIo, IoStatesAreNotPartOfStateEquality)
{
    AppState a, b;
    b.ioStates["x"] = io("x", "on");
    b.config.name = "other";
    EXPECT_TRUE(a == b);
}

TEST(AppStoreSubscribers, UnsubscribeStopsNotifications)
{
    const std::string sub_a = uid("sub_a");
    int calls = 0;
    auto id = store().subscribe([&](const AppState&) { calls++; });
    receiveOne(io(sub_a, "1"));
    EXPECT_EQ(calls, 1);

    store().unsubscribe(id);
    receiveOne(io(sub_a, "2"));
    EXPECT_EQ(calls, 1);
}

TEST(AppStoreSubscribers, CallbackReceivesTheUpdatedState)
{
    const std::string sub_state = uid("sub_state");
    std::string seen;
    auto id = store().subscribe([&](const AppState& st)
    {
        auto it = st.ioStates.find(sub_state);
        if (it != st.ioStates.end())
            seen = it->second.state;
    });
    receiveOne(io(sub_state, "value"));
    store().unsubscribe(id);
    EXPECT_EQ(seen, "value");
}

TEST(AppStoreSubscribers, CallbackMayUnsubscribeItself)
{
    const std::string sub_self = uid("sub_self");
    int calls = 0;
    SubscriptionId id = 0;
    id = store().subscribe([&](const AppState&)
    {
        calls++;
        store().unsubscribe(id);
    });
    receiveOne(io(sub_self, "1"));
    receiveOne(io(sub_self, "2"));
    EXPECT_EQ(calls, 1);
}

TEST(AppStoreSubscribers, CallbackMayReadTheStore)
{
    const std::string sub_reenter = uid("sub_reenter");
    std::string seen;
    auto id = store().subscribe([&](const AppState&)
    {
        auto it = store().getState().ioStates.find(sub_reenter);
        if (it != store().getState().ioStates.end())
            seen = it->second.state;
    });
    receiveOne(io(sub_reenter, "ok"));
    store().unsubscribe(id);
    EXPECT_EQ(seen, "ok");
}

TEST(AppStoreConfig, ConfigUpdateReplacesTheWholeConfig)
{
    CalaosProtocol::RemoteUIConfig cfg;
    cfg.name = "Panel";
    cfg.brightness = 30;
    cfg.pages_json = R"({"pages": []})";
    store().handleEvent(AppEvent(AppEventType::ConfigUpdateReceived, ConfigUpdateReceivedData{cfg}));

    auto st = store().getStateCopy();
    EXPECT_EQ(st.config.name, "Panel");
    EXPECT_EQ(st.config.brightness, 30);
    EXPECT_EQ(st.config.pages_json, R"({"pages": []})");

    CalaosProtocol::RemoteUIConfig second;
    store().handleEvent(AppEvent(AppEventType::ConfigUpdateReceived, ConfigUpdateReceivedData{second}));
    EXPECT_EQ(store().getStateCopy().config.name, "");
    EXPECT_EQ(store().getStateCopy().config.brightness, 80);
}

TEST(AppStoreConfig, BrightnessEventOnlyTouchesBrightness)
{
    CalaosProtocol::RemoteUIConfig cfg;
    cfg.name = "Keep";
    store().handleEvent(AppEvent(AppEventType::ConfigUpdateReceived, ConfigUpdateReceivedData{cfg}));

    store().handleEvent(AppEvent(AppEventType::BrightnessChanged, BrightnessChangedData{25}));
    auto st = store().getStateCopy();
    EXPECT_EQ(st.config.brightness, 25);
    EXPECT_EQ(st.config.name, "Keep");
}

TEST(AppStoreNetwork, TimeoutIsIgnoredOnceConnected)
{
    store().handleEvent(AppEvent(AppEventType::NetworkDisconnected));
    store().handleEvent(AppEvent(AppEventType::NetworkTimeout));
    EXPECT_TRUE(store().getStateCopy().network.hasTimeout);

    NetworkIpAssignedData ip;
    ip.connectionType = NetworkConnectionType::Ethernet;
    ip.ipAddress = "10.0.0.2";
    store().handleEvent(AppEvent(AppEventType::NetworkIpAssigned, ip));
    auto st = store().getStateCopy();
    EXPECT_TRUE(st.network.isReady);
    EXPECT_TRUE(st.network.isConnected);
    EXPECT_FALSE(st.network.hasTimeout);
    EXPECT_EQ(st.network.ipAddress, "10.0.0.2");

    store().handleEvent(AppEvent(AppEventType::NetworkTimeout));
    EXPECT_FALSE(store().getStateCopy().network.hasTimeout);
}

TEST(AppStoreNetwork, DisconnectClearsAddressing)
{
    NetworkIpAssignedData ip;
    ip.ipAddress = "10.0.0.3";
    ip.gateway = "10.0.0.1";
    store().handleEvent(AppEvent(AppEventType::NetworkIpAssigned, ip));

    store().handleEvent(AppEvent(AppEventType::NetworkDisconnected));
    auto st = store().getStateCopy();
    EXPECT_FALSE(st.network.isConnected);
    EXPECT_FALSE(st.network.isReady);
    EXPECT_TRUE(st.network.ipAddress.empty());
    EXPECT_TRUE(st.network.gateway.empty());
    EXPECT_EQ(st.network.connectionType, NetworkConnectionType::None);
}

TEST(AppStoreServer, FirstDiscoveredServerIsSelectedAndDuplicatesAreIgnored)
{
    CalaosServerFoundData first;
    first.serverIp = "192.168.1.10";
    first.serverPort = 5454;
    first.serverSsl = true;
    CalaosServerFoundData second;
    second.serverIp = "192.168.1.11";
    second.serverPort = 8080;

    store().handleEvent(AppEvent(AppEventType::CalaosServerFound, first));
    store().handleEvent(AppEvent(AppEventType::CalaosServerFound, first));
    store().handleEvent(AppEvent(AppEventType::CalaosServerFound, second));

    auto st = store().getStateCopy();
    EXPECT_EQ(st.calaosServer.discoveredServers.size(), 2u);
    EXPECT_EQ(st.calaosServer.selectedServer, "192.168.1.10");
    EXPECT_TRUE(st.calaosServer.serverSsl);
    EXPECT_EQ(st.calaosServer.serverPort, 5454);
}

TEST(AppStoreWebSocket, ConnectedStampsTheHalClock)
{
    FakeHal::nowMs = 123456;
    store().handleEvent(AppEvent(AppEventType::WebSocketConnecting));
    EXPECT_TRUE(store().getStateCopy().websocket.isConnecting);

    store().handleEvent(AppEvent(AppEventType::WebSocketConnected));
    auto st = store().getStateCopy();
    EXPECT_TRUE(st.websocket.isConnected);
    EXPECT_FALSE(st.websocket.isConnecting);
    EXPECT_EQ(st.websocket.lastConnectedTimestamp, 123456u);
}

TEST(AppStoreWebSocket, AuthFailureRequiresReprovisioningOnlyForCredentialErrors)
{
    WebSocketAuthFailedData bad;
    bad.errorType = WebSocketAuthErrorType::InvalidHmac;
    store().handleEvent(AppEvent(AppEventType::WebSocketAuthFailed, bad));
    auto st = store().getStateCopy();
    EXPECT_TRUE(st.websocket.authFailed);
    EXPECT_TRUE(st.websocket.requiresReProvisioning());
    EXPECT_FALSE(st.websocket.isRetryableError());

    WebSocketAuthFailedData limited;
    limited.errorType = WebSocketAuthErrorType::RateLimited;
    store().handleEvent(AppEvent(AppEventType::WebSocketAuthFailed, limited));
    st = store().getStateCopy();
    EXPECT_FALSE(st.websocket.requiresReProvisioning());
    EXPECT_TRUE(st.websocket.isRetryableError());
    EXPECT_EQ(st.websocket.getRetryDelayMs(), 60000);
}

TEST(AppStoreMisc, NotificationBumpsTheVersionEvenForTheSameMessage)
{
    store().handleEvent(AppEvent(AppEventType::NotificationReceived, NotificationReceivedData{"hello"}));
    auto v1 = store().getStateCopy().notificationVersion;
    store().handleEvent(AppEvent(AppEventType::NotificationReceived, NotificationReceivedData{"hello"}));
    auto st = store().getStateCopy();
    EXPECT_EQ(st.notificationMessage, "hello");
    EXPECT_EQ(st.notificationVersion, v1 + 1);
}

TEST(AppStoreMisc, RelayStatesAreTrackedPerRelay)
{
    store().handleEvent(AppEvent(AppEventType::RelayStateChanged, RelayStateChangedData{1, true}));
    store().handleEvent(AppEvent(AppEventType::RelayStateChanged, RelayStateChangedData{2, false}));
    auto st = store().getStateCopy();
    EXPECT_TRUE(st.relayStates.at(1));
    EXPECT_FALSE(st.relayStates.at(2));
}

TEST(AppStoreMisc, EventWithoutItsPayloadIsIgnored)
{
    Subscription sub;
    store().handleEvent(AppEvent(AppEventType::IoStateReceived));
    store().handleEvent(AppEvent(AppEventType::ConfigUpdateReceived));
    EXPECT_EQ(sub.calls, 0);
}

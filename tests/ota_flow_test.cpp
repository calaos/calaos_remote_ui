#include <gtest/gtest.h>
#include "app_dispatcher.h"
#include "app_store.h"
#include "ota_manager.h"
#include "version.h"
#include "support/event_probe.h"
#include "support/fake_ota.h"
#include "support/fake_hal.h"
#include "ota_policy.h"
#include <cstdint>

namespace
{
AppStore& store() { return AppStore::getInstance(); }

OtaStatus otaStatus() { return store().getStateCopy().ota.status; }

void dispatch(const AppEvent& e) { AppDispatcher::getInstance().dispatch(e); }

// A subscriber may dispatch while handling an event (OtaManager does): those
// land behind the probe's sentinel, so drain a few rounds.
void settle()
{
    for (int i = 0; i < 3; i++)
        EventProbe::instance().settle();
}

OtaUpdateAvailableData offer(const std::string& version)
{
    OtaUpdateAvailableData d;
    d.hardwareId = "linux-dev";
    d.version = version;
    d.checksumSha256 = "00";
    d.downloadUrl = "http://calaos:5454/api/v3/ota/firmware/linux-dev/download";
    return d;
}

// Every test leaves the singletons with no OTA running.
void backToIdle()
{
    dispatch(AppEvent(AppEventType::OtaError, OtaErrorData{"test cleanup"}));
    dispatch(AppEvent(AppEventType::OtaReset));
    settle();
}

FakeOta& backend()
{
    EventProbe::instance();
    OtaManager::getInstance().init();
    return *FakeOta::instance();
}
}

// The offer equals APP_VERSION, so OtaManager never acts on it: only the reducer is measured.
TEST(OtaStore, AnOfferDuringADownloadDoesNotTurnItBackIntoAPendingOffer)
{
    store().handleEvent(AppEvent(AppEventType::OtaUpdateAvailable, offer(APP_VERSION)));
    store().handleEvent(AppEvent(AppEventType::OtaDownloadStarted));
    ASSERT_EQ(otaStatus(), OtaStatus::Downloading);

    // What the server sends again when the panel reconnects mid-download
    store().handleEvent(AppEvent(AppEventType::OtaUpdateAvailable, offer(APP_VERSION)));
    EXPECT_EQ(otaStatus(), OtaStatus::Downloading);

    backToIdle();
}

TEST(OtaStore, TheErrorScreenResetDoesNotCancelADownloadThatStartedSince)
{
    store().handleEvent(AppEvent(AppEventType::OtaUpdateAvailable, offer(APP_VERSION)));
    store().handleEvent(AppEvent(AppEventType::OtaDownloadStarted));

    store().handleEvent(AppEvent(AppEventType::OtaReset));
    EXPECT_EQ(otaStatus(), OtaStatus::Downloading);

    backToIdle();
    EXPECT_EQ(otaStatus(), OtaStatus::Idle);
}

TEST(OtaManagerFlow, AnOfferTheBackendCannotRunIsNotLeftPending)
{
    FakeOta& ota = backend();
    ota.supported = false;

    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("1.0.0-unsupported")));
    settle();
    ota.supported = true;

    // Available holds the main page back: left there, the panel shows the logo for good
    EXPECT_NE(otaStatus(), OtaStatus::Available);
    EXPECT_FALSE(store().getStateCopy().ota.updateAvailable);
    backToIdle();
}

namespace
{
void serverConnected(bool connected)
{
    if (connected)
        dispatch(AppEvent(AppEventType::WebSocketConnected));
    else
        dispatch(AppEvent(AppEventType::WebSocketDisconnected, WebSocketDisconnectedData{"test", 0}));
}
}

// The reported sequence: an upgrade restarts the server while the panels download.
TEST(OtaManagerFlow, AServerRestartMidDownloadEndsOnTheMainPageAndRetriesLater)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 1000000;
    serverConnected(true);
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("2.0.0-restart")));
    settle();
    const int starts = ota.starts();
    ASSERT_EQ(otaStatus(), OtaStatus::Downloading);
    EXPECT_FALSE(OtaPolicy::mayShowMainPage(store().getStateCopy()));

    // The server goes away, comes back and offers again before the old
    // download has timed out
    serverConnected(false);
    serverConnected(true);
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("2.0.0-restart")));
    settle();
    EXPECT_EQ(otaStatus(), OtaStatus::Downloading);
    EXPECT_EQ(ota.starts(), starts);

    ota.report(HalOtaState::Error, "Connection lost");
    settle();
    dispatch(AppEvent(AppEventType::OtaReset));   // the error screen closing
    settle();

    EXPECT_EQ(otaStatus(), OtaStatus::Idle);
    EXPECT_TRUE(OtaPolicy::mayShowMainPage(store().getStateCopy()));
    ASSERT_TRUE(OtaManager::getInstance().retryScheduled());

    // Not before the delay
    OtaManager::getInstance().tick(FakeHal::nowMs + OtaRetryPolicy::kBaseDelayMs - 1);
    settle();
    EXPECT_EQ(ota.starts(), starts);

    OtaManager::getInstance().tick(FakeHal::nowMs + OtaRetryPolicy::kBaseDelayMs);
    settle();
    EXPECT_EQ(ota.starts(), starts + 1);
    EXPECT_EQ(ota.lastUrl(), offer("x").downloadUrl);
    EXPECT_FALSE(OtaManager::getInstance().retryScheduled());

    ota.report(HalOtaState::Error, "cleanup");
    backToIdle();
}

TEST(OtaManagerFlow, ARetryWaitsForTheServerConnection)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 2000000;
    serverConnected(true);
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("2.0.0-wait")));
    settle();
    ota.report(HalOtaState::Error, "Connection failed");
    settle();
    serverConnected(false);
    settle();
    const int starts = ota.starts();

    const uint64_t late = FakeHal::nowMs + OtaRetryPolicy::kMaxDelayMs;
    OtaManager::getInstance().tick(late);
    settle();
    EXPECT_EQ(ota.starts(), starts);
    EXPECT_TRUE(OtaManager::getInstance().retryScheduled());

    serverConnected(true);
    settle();
    OtaManager::getInstance().tick(late);
    settle();
    EXPECT_EQ(ota.starts(), starts + 1);

    ota.report(HalOtaState::Error, "cleanup");
    backToIdle();
}

TEST(OtaManagerFlow, AStartTheBackendRefusesIsAnErrorWithARetry)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 3000000;
    serverConnected(true);
    ota.refuseStart = true;
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("2.0.0-refused")));
    settle();
    ota.refuseStart = false;

    EXPECT_EQ(otaStatus(), OtaStatus::Error);
    EXPECT_TRUE(OtaManager::getInstance().retryScheduled());
    EXPECT_FALSE(OtaManager::getInstance().isUpdateInProgress());
    backToIdle();
}

TEST(OtaStore, TheErrorScreenResetEndsAnError)
{
    store().handleEvent(AppEvent(AppEventType::OtaError, OtaErrorData{"x"}));
    store().handleEvent(AppEvent(AppEventType::OtaReset));
    EXPECT_EQ(otaStatus(), OtaStatus::Idle);
    EXPECT_FALSE(store().getStateCopy().ota.updateAvailable);
}

TEST(OtaPolicy, OnlyARunningUpdateHoldsTheMainPageBack)
{
    AppState s;
    s.websocket.isConnected = true;
    for (OtaStatus st : {OtaStatus::Idle, OtaStatus::Available, OtaStatus::Error})
    {
        s.ota.status = st;
        EXPECT_TRUE(OtaPolicy::mayShowMainPage(s)) << static_cast<int>(st);
    }
    for (OtaStatus st : {OtaStatus::Downloading, OtaStatus::Installing, OtaStatus::Rebooting})
    {
        s.ota.status = st;
        EXPECT_FALSE(OtaPolicy::mayShowMainPage(s)) << static_cast<int>(st);
    }
    s.ota.status = OtaStatus::Idle;
    s.websocket.isConnected = false;
    EXPECT_FALSE(OtaPolicy::mayShowMainPage(s));
}

TEST(OtaRetryPolicy, TheDelayDoublesUpToTheCeiling)
{
    EXPECT_EQ(OtaRetryPolicy::delayForFailures(1), OtaRetryPolicy::kBaseDelayMs);
    EXPECT_EQ(OtaRetryPolicy::delayForFailures(2), 2 * OtaRetryPolicy::kBaseDelayMs);
    EXPECT_EQ(OtaRetryPolicy::delayForFailures(3), 4 * OtaRetryPolicy::kBaseDelayMs);
    EXPECT_EQ(OtaRetryPolicy::delayForFailures(50), OtaRetryPolicy::kMaxDelayMs);

    OtaRetryPolicy p;
    EXPECT_FALSE(p.due(UINT64_MAX));
    p.onFailure(100);
    EXPECT_FALSE(p.due(100 + OtaRetryPolicy::kBaseDelayMs - 1));
    EXPECT_TRUE(p.due(100 + OtaRetryPolicy::kBaseDelayMs));
    p.consume();
    EXPECT_FALSE(p.due(UINT64_MAX));
    p.onFailure(0);
    EXPECT_EQ(p.failures(), 2);
    EXPECT_EQ(p.nextAttemptMs(), 2 * OtaRetryPolicy::kBaseDelayMs);
    p.reset();
    EXPECT_EQ(p.failures(), 0);
    EXPECT_FALSE(p.armed());
}

TEST(OtaManagerFlow, ANewVersionStartsTheBackoffAgain)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 4000000;
    serverConnected(true);
    for (int i = 0; i < 3; i++)
    {
        dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("3.0.0-old")));
        settle();
        ota.report(HalOtaState::Error, "fail");
        settle();
    }
    EXPECT_EQ(OtaManager::getInstance().nextRetryMs(), FakeHal::nowMs + OtaRetryPolicy::delayForFailures(3));

    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer("3.0.1-new")));
    settle();
    ota.report(HalOtaState::Error, "fail");
    settle();
    EXPECT_EQ(OtaManager::getInstance().nextRetryMs(), FakeHal::nowMs + OtaRetryPolicy::kBaseDelayMs);
    backToIdle();
}

namespace
{
OtaUpdateAvailableData offerWith(const std::string& version, const std::string& checksum)
{
    OtaUpdateAvailableData d = offer(version);
    d.checksumSha256 = checksum;
    return d;
}
}

// The URL serves whatever image the server has now: retrying the old
// checksum against it fails on every attempt.
TEST(OtaManagerFlow, ANewVersionOfferedDuringADownloadIsTheOneRetried)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 5000000;
    serverConnected(true);
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offerWith("5.0.0-a", "aa")));
    settle();
    ASSERT_EQ(otaStatus(), OtaStatus::Downloading);
    const int starts = ota.starts();

    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offerWith("5.0.1-b", "bb")));
    settle();
    EXPECT_EQ(ota.starts(), starts);

    ota.report(HalOtaState::Error, "Connection lost");
    settle();
    OtaManager::getInstance().tick(FakeHal::nowMs + OtaRetryPolicy::kBaseDelayMs);
    settle();
    EXPECT_EQ(ota.starts(), starts + 1);
    EXPECT_EQ(ota.lastChecksum(), "bb");
    EXPECT_EQ(store().getStateCopy().ota.version, "5.0.1-b");

    ota.report(HalOtaState::Error, "cleanup");
    backToIdle();
}

// The dispatcher drops events when its queue is full: a lost OtaError must
// not leave the update screen up for good.
TEST(OtaManagerFlow, ADownloadStateLeftWithoutABackendIsEnded)
{
    backend();
    FakeHal::nowMs = 6000000;
    store().handleEvent(AppEvent(AppEventType::OtaUpdateAvailable, offer(APP_VERSION)));
    store().handleEvent(AppEvent(AppEventType::OtaDownloadStarted));
    ASSERT_EQ(otaStatus(), OtaStatus::Downloading);

    OtaManager::getInstance().tick(FakeHal::nowMs);
    OtaManager::getInstance().tick(FakeHal::nowMs + 9000);
    settle();
    EXPECT_EQ(otaStatus(), OtaStatus::Downloading);

    OtaManager::getInstance().tick(FakeHal::nowMs + 10000);
    settle();
    EXPECT_EQ(otaStatus(), OtaStatus::Error);
    backToIdle();
}

TEST(OtaManagerFlow, AnOfferWithoutChecksumIsRefused)
{
    FakeOta& ota = backend();
    serverConnected(true);
    settle();
    const int starts = ota.starts();
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offerWith("7.0.0-nosum", "")));
    settle();
    EXPECT_EQ(ota.starts(), starts);
    EXPECT_NE(otaStatus(), OtaStatus::Available);
    backToIdle();
}

TEST(OtaManagerFlow, APermanentErrorWaitsForTheServerToOfferAgain)
{
    FakeOta& ota = backend();
    FakeHal::nowMs = 8000000;
    serverConnected(true);
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offerWith("8.0.0-perm", "cc")));
    settle();
    const int starts = ota.starts();
    ota.report(HalOtaState::Error, "Checksum mismatch", true);
    settle();
    EXPECT_FALSE(OtaManager::getInstance().retryScheduled());

    OtaManager::getInstance().tick(FakeHal::nowMs + OtaRetryPolicy::kMaxDelayMs * 4);
    settle();
    EXPECT_EQ(ota.starts(), starts);

    // The server offers again (reconnection, rescan): that one is downloaded
    dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offerWith("8.0.0-perm", "cc")));
    settle();
    EXPECT_EQ(ota.starts(), starts + 1);

    // A transient error after it keeps the backoff
    ota.report(HalOtaState::Error, "Connection lost");
    settle();
    EXPECT_TRUE(OtaManager::getInstance().retryScheduled());
    OtaManager::getInstance().tick(FakeHal::nowMs + OtaRetryPolicy::kBaseDelayMs);
    settle();
    EXPECT_EQ(ota.starts(), starts + 2);

    ota.report(HalOtaState::Error, "cleanup");
    backToIdle();
}

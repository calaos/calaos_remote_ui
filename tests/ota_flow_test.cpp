#include <gtest/gtest.h>
#include "app_dispatcher.h"
#include "app_store.h"
#include "ota_manager.h"
#include "version.h"
#include "support/event_probe.h"
#include "support/fake_ota.h"

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

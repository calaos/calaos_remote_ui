/**
 * @file ota_policy.h
 * @brief OTA decisions kept free of LVGL and of the OTA backend
 */

#pragma once

#include "../flux/app_store.h"
#include <cstdint>

namespace OtaPolicy
{

// A download, an install or the reboot owns the screen. A pending offer does
// not: it may never start (unsupported backend, refused start).
inline bool ownsScreen(const OtaState& ota)
{
    return ota.status == OtaStatus::Downloading ||
           ota.status == OtaStatus::Installing ||
           ota.status == OtaStatus::Rebooting;
}

// The startup page hands over to the main page whenever this holds, whatever
// sequence of events led there.
inline bool mayShowMainPage(const AppState& state)
{
    return state.websocket.isConnected && !ownsScreen(state.ota);
}

}

// Delay before the panel downloads again an offer that failed. The server may
// be restarting (the case of an upgrade that ships both server and firmware),
// so the delay doubles up to a ceiling instead of hammering it.
class OtaRetryPolicy
{
public:
    static constexpr uint64_t kBaseDelayMs = 30 * 1000;
    static constexpr uint64_t kMaxDelayMs = 30 * 60 * 1000;

    void onFailure(uint64_t nowMs);
    void reset();

    bool armed() const { return armed_; }
    bool due(uint64_t nowMs) const { return armed_ && nowMs >= nextAttemptMs_; }
    // Consumes the scheduled attempt; the failure count stays for the next delay.
    void consume() { armed_ = false; }

    int failures() const { return failures_; }
    uint64_t nextAttemptMs() const { return nextAttemptMs_; }
    static uint64_t delayForFailures(int failures);

private:
    int failures_ = 0;
    bool armed_ = false;
    uint64_t nextAttemptMs_ = 0;
};

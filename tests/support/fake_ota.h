#pragma once

#include "hal_ota.h"
#include <atomic>
#include <mutex>
#include <string>

// The OTA backend OtaManager gets from createOta() in the host tests. It never
// downloads anything: a test plays the backend's part with report().
class FakeOta : public HalOta
{
public:
    // The instance OtaManager owns; created on its first init().
    static FakeOta* instance();

    HalResult startUpdate(const std::string& url,
                          const std::string& expectedChecksum,
                          const std::map<std::string, std::string>& authHeaders,
                          HalOtaProgressCallback progressCallback) override;
    HalResult abortUpdate() override { return HalResult::OK; }
    HalOtaProgress getProgress() const override { return {}; }
    HalOtaState getState() const override { return HalOtaState::Idle; }
    bool isSupported() const override { return supported.load(); }
    HalResult markFirmwareValid() override { return HalResult::OK; }

    // Delivers a progress report the way the real backend does, from its own thread.
    void report(HalOtaState state, const std::string& error = "");

    int starts() const { return starts_.load(); }
    std::string lastUrl() const;

    std::atomic<bool> supported{true};
    std::atomic<bool> refuseStart{false};

private:
    mutable std::mutex mutex_;
    HalOtaProgressCallback callback_;
    std::string lastUrl_;
    std::atomic<int> starts_{0};
};

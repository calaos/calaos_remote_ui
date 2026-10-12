#pragma once

#include "hal.h"
#include <cstdint>

class FakeHal : public HAL
{
public:
    static FakeHal& instance();

    // Value returned by HalSystem::getTimeMs().
    static uint64_t nowMs;

    HalResult init() override { return HalResult::OK; }
    HalResult initEssentials() override { return HalResult::OK; }
    HalResult initNetworkAsync() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }

    HalDisplay& getDisplay() override;
    HalInput& getInput() override;
    HalNetwork& getNetwork() override;
    HalSystem& getSystem() override;
    HalRelay& getRelay() override;

    bool isNetworkReady() const override { return true; }

private:
    FakeHal() = default;
};

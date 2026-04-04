#pragma once

#include "../hal_relay.h"
#include "board_config.h"

class LinuxHalRelay : public HalRelay
{
public:
    HalResult init() override;
    HalResult deinit() override;

    int getRelayCount() const override { return BOARD_RELAY_COUNT; }
    HalResult setRelay(int relay, bool state) override;
    bool getRelayState(int relay) const override;

private:
    bool relayStates_[BOARD_RELAY_COUNT > 0 ? BOARD_RELAY_COUNT : 1] = {};
};

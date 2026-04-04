#pragma once

#include "hal_types.h"

class HalRelay
{
public:
    virtual ~HalRelay() = default;

    virtual HalResult init() = 0;
    virtual HalResult deinit() = 0;

    virtual int getRelayCount() const = 0;

    /**
     * @brief Set relay state
     * @param relay Relay number (1-indexed)
     * @param state true = ON, false = OFF
     */
    virtual HalResult setRelay(int relay, bool state) = 0;

    /**
     * @brief Get current relay state
     * @param relay Relay number (1-indexed)
     * @return true if relay is ON
     */
    virtual bool getRelayState(int relay) const = 0;

protected:
    HalRelay() = default;
};

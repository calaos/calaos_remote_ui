#pragma once

#include "hal_types.h"
#include "hal_display.h"
#include "hal_input.h"
#include "hal_network.h"
#include "hal_system.h"
#include <memory>

class HAL
{
public:
    static HAL& getInstance();

    virtual ~HAL() = default;

    virtual HalResult init() = 0;
    virtual HalResult initEssentials() = 0;
    virtual HalResult initNetworkAsync() = 0;
    virtual HalResult deinit() = 0;

    virtual HalDisplay& getDisplay() = 0;
    virtual HalInput& getInput() = 0;
    virtual HalNetwork& getNetwork() = 0;
    virtual HalSystem& getSystem() = 0;

    virtual bool isNetworkReady() const = 0;

protected:
    HAL();

    void initLogger();

private:
    HAL(const HAL&) = delete;
    HAL& operator=(const HAL&) = delete;
};
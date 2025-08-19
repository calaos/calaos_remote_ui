#pragma once

#include "../hal.h"
#include "esp32_hal_display.h"
#include "esp32_hal_input.h"
#include "esp32_hal_network.h"
#include "esp32_hal_system.h"
#include <memory>

class Esp32HAL : public HAL
{
public:
    static Esp32HAL& getInstance();
    
    HalResult init() override;
    HalResult deinit() override;
    
    HalDisplay& getDisplay() override;
    HalInput& getInput() override;
    HalNetwork& getNetwork() override;
    HalSystem& getSystem() override;

private:
    Esp32HAL() = default;
    
    std::unique_ptr<Esp32HalDisplay> display;
    std::unique_ptr<Esp32HalInput> input;
    std::unique_ptr<Esp32HalNetwork> network;
    std::unique_ptr<Esp32HalSystem> system;
};
#pragma once

#include "../hal.h"
#include "linux_hal_display.h"
#include "linux_hal_input.h"
#include "linux_hal_network.h"
#include "linux_hal_system.h"
#include <memory>

class LinuxHAL : public HAL {
public:
    static LinuxHAL& getInstance();
    
    HalResult init() override;
    HalResult initEssentials() override;
    HalResult initNetworkAsync() override;
    HalResult deinit() override;
    
    HalDisplay& getDisplay() override;
    HalInput& getInput() override;
    HalNetwork& getNetwork() override;
    HalSystem& getSystem() override;
    
    bool isNetworkReady() const override;

private:
    LinuxHAL() = default;
    
    std::unique_ptr<LinuxHalDisplay> display_;
    std::unique_ptr<LinuxHalInput> input_;
    std::unique_ptr<LinuxHalNetwork> network_;
    std::unique_ptr<LinuxHalSystem> system_;
    bool networkReady_ = false;
};
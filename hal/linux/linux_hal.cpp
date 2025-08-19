#include "linux_hal.h"
#include <iostream>

LinuxHAL& LinuxHAL::getInstance() {
    static LinuxHAL instance;
    return instance;
}

HalResult LinuxHAL::init() {
    try {
        std::cout << "Initializing Linux HAL" << std::endl;
        
        // Initialize system first
        system_ = std::make_unique<LinuxHalSystem>();
        if (system_->init() != HalResult::OK) {
            std::cerr << "Failed to init system HAL" << std::endl;
            return HalResult::ERROR;
        }
        
        // Initialize display
        display_ = std::make_unique<LinuxHalDisplay>();
        if (display_->init() != HalResult::OK) {
            std::cerr << "Failed to init display HAL" << std::endl;
            return HalResult::ERROR;
        }
        
        // Initialize input
        input_ = std::make_unique<LinuxHalInput>();
        if (input_->init() != HalResult::OK) {
            std::cerr << "Failed to init input HAL" << std::endl;
            return HalResult::ERROR;
        }
        
        // Initialize network
        network_ = std::make_unique<LinuxHalNetwork>();
        if (network_->init() != HalResult::OK) {
            std::cerr << "Failed to init network HAL" << std::endl;
            return HalResult::ERROR;
        }
        
        std::cout << "Linux HAL initialized successfully" << std::endl;
        return HalResult::OK;
    } catch (...) {
        std::cerr << "Exception during Linux HAL init" << std::endl;
        return HalResult::ERROR;
    }
}

HalResult LinuxHAL::deinit() {
    if (network_) network_->deinit();
    if (input_) input_->deinit();
    if (display_) display_->deinit();
    if (system_) system_->deinit();
    
    network_.reset();
    input_.reset();
    display_.reset();
    system_.reset();
    
    std::cout << "Linux HAL deinitialized" << std::endl;
    return HalResult::OK;
}

HalDisplay& LinuxHAL::getDisplay() {
    return *display_;
}

HalInput& LinuxHAL::getInput() {
    return *input_;
}

HalNetwork& LinuxHAL::getNetwork() {
    return *network_;
}

HalSystem& LinuxHAL::getSystem() {
    return *system_;
}
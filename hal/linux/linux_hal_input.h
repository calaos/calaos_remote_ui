#pragma once

#include "../hal_input.h"
#include "lv_conf_platform.h"

typedef enum {
    CALAOS_INPUT_BACKEND_NONE = 0,
    CALAOS_INPUT_BACKEND_EVDEV,
    CALAOS_INPUT_BACKEND_LIBINPUT
} calaos_input_backend_t;

class LinuxHalInput : public HalInput
{
public:
    HalResult init() override;
    HalResult deinit() override;
    lv_indev_t* getLvglInputDevice() override;
    
    // Backend-specific methods
    void setInputBackendOverride(const std::string& backend);
    std::string getCurrentInputBackend() const;

private:
    lv_indev_t* inputDevice = nullptr;
    calaos_input_backend_t currentInputBackend = CALAOS_INPUT_BACKEND_NONE;
    calaos_input_backend_t backendOverride = CALAOS_INPUT_BACKEND_NONE;
    
    // Backend detection and selection
    calaos_input_backend_t detectBestInputBackend();
    bool isInputBackendAvailable(calaos_input_backend_t backend);
    std::string getInputBackendName(calaos_input_backend_t backend) const;
    calaos_input_backend_t getInputBackendFromEnv();
    
    // Backend-specific initialization
    HalResult initEvdevBackend();
    HalResult initLibinputBackend();
};
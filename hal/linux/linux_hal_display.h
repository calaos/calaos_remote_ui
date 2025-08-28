#pragma once

#include "../hal_display.h"
#include "display_backend_selector.h"
#include <mutex>

class LinuxHalDisplay : public HalDisplay
{
public:
    HalResult init() override;
    HalResult deinit() override;
    DisplayInfo getDisplayInfo() const override;
    HalResult setBacklight(uint8_t brightness) override;
    HalResult backlightOn() override;
    HalResult backlightOff() override;
    void lock(uint32_t timeoutMs = 0) override;
    void unlock() override;
    lv_display_t* getLvglDisplay() override;

    // Backend-specific methods
    void setBackendOverride(const std::string& backend);
    std::string getCurrentBackend() const;

private:
    lv_display_t* display = nullptr;
    DisplayInfo displayInfo;
    std::recursive_timed_mutex displayMutex;
    calaos_display_backend_t currentBackend = CALAOS_DISPLAY_BACKEND_NONE;

    // Backend-specific initialization
    HalResult initFbdevBackend();
    HalResult initDrmBackend();
    HalResult initSdlBackend();
    HalResult initX11Backend();
    HalResult initGlfw3Backend();

    // Backend-specific cleanup
    void deinitFbdevBackend();
    void deinitDrmBackend();
    void deinitSdlBackend();
    void deinitX11Backend();
    void deinitGlfw3Backend();

    // Framebuffer backend data
    uint8_t* fbBuffer = nullptr;
    int fbFd = -1;
    size_t fbSize = 0;

    // Generic backend data
    void* backendData = nullptr;
};
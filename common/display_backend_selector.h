#pragma once

#ifndef ESP_PLATFORM

#include "lv_conf_platform.h"
#include <string>
#include <vector>

typedef enum {
    CALAOS_DISPLAY_BACKEND_NONE = 0,
    CALAOS_DISPLAY_BACKEND_FBDEV,
    CALAOS_DISPLAY_BACKEND_DRM,
    CALAOS_DISPLAY_BACKEND_SDL,
    CALAOS_DISPLAY_BACKEND_X11,
    CALAOS_DISPLAY_BACKEND_GLES
} calaos_display_backend_t;

class DisplayBackendSelector
{
public:
    static DisplayBackendSelector& getInstance();

    // Backend detection and selection
    calaos_display_backend_t detectBestBackend();
    std::vector<calaos_display_backend_t> getAvailableBackends();

    // Environment variable and command line handling
    calaos_display_backend_t getBackendFromEnv();
    void setBackendOverride(const std::string& backendName);
    void setBackendOverride(calaos_display_backend_t backend);

    // Backend information
    std::string getBackendName(calaos_display_backend_t backend);
    bool isBackendAvailable(calaos_display_backend_t backend);
    void listAvailableBackends();

    // Backend initialization
    bool initializeBackend(calaos_display_backend_t backend);

private:
    DisplayBackendSelector() = default;

    calaos_display_backend_t backendOverride = CALAOS_DISPLAY_BACKEND_NONE;

    // Backend detection helpers
    bool checkFbdevAvailable();
    bool checkDrmAvailable();
    bool checkSdlAvailable();
    bool checkX11Available();
    bool checkGlfw3Available();

    // Priority order for Linux systems
    std::vector<calaos_display_backend_t> getLinuxPriority();
};

#endif // ESP_PLATFORM
#include "display_backend_selector.h"

#ifndef ESP_PLATFORM

#include "logging.h"
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>

static const char* TAG = "gfx";

DisplayBackendSelector& DisplayBackendSelector::getInstance()
{
    static DisplayBackendSelector instance;
    return instance;
}

calaos_display_backend_t DisplayBackendSelector::detectBestBackend()
{
    // Check for environment variable override first
    calaos_display_backend_t envBackend = getBackendFromEnv();
    if (envBackend != CALAOS_DISPLAY_BACKEND_NONE)
    {
        if (isBackendAvailable(envBackend))
        {
            ESP_LOGI(TAG, "Using backend from environment: %s", getBackendName(envBackend).c_str());
            return envBackend;
        }
        else
        {
            ESP_LOGW(TAG, "Requested backend %s not available, falling back to auto-detection",
                     getBackendName(envBackend).c_str());
        }
    }

    // Check for override
    if (backendOverride != CALAOS_DISPLAY_BACKEND_NONE && isBackendAvailable(backendOverride))
    {
        ESP_LOGI(TAG, "Using override backend: %s", getBackendName(backendOverride).c_str());
        return backendOverride;
    }

    // Get priority list for Linux systems
    std::vector<calaos_display_backend_t> priorities = getLinuxPriority();

    // Find first available backend
    for (auto backend : priorities)
    {
        if (isBackendAvailable(backend))
        {
            ESP_LOGI(TAG, "Selected backend: %s", getBackendName(backend).c_str());
            return backend;
        }
    }

    ESP_LOGE(TAG, "No suitable display backend found!");
    return CALAOS_DISPLAY_BACKEND_NONE;
}

std::vector<calaos_display_backend_t> DisplayBackendSelector::getAvailableBackends()
{
    std::vector<calaos_display_backend_t> available;

    std::vector<calaos_display_backend_t> allBackends = {
        CALAOS_DISPLAY_BACKEND_GLES,
        CALAOS_DISPLAY_BACKEND_DRM,
        CALAOS_DISPLAY_BACKEND_FBDEV,
        CALAOS_DISPLAY_BACKEND_X11,
        CALAOS_DISPLAY_BACKEND_SDL
    };

    for (auto backend : allBackends)
    {
        if (isBackendAvailable(backend))
            available.push_back(backend);
    }

    return available;
}

calaos_display_backend_t DisplayBackendSelector::getBackendFromEnv()
{
    const char* envVar = std::getenv("CALAOS_DISPLAY_BACKEND");
    if (!envVar) return CALAOS_DISPLAY_BACKEND_NONE;

    std::string backend(envVar);

    if (backend == "fbdev") return CALAOS_DISPLAY_BACKEND_FBDEV;
    if (backend == "drm") return CALAOS_DISPLAY_BACKEND_DRM;
    if (backend == "sdl") return CALAOS_DISPLAY_BACKEND_SDL;
    if (backend == "x11") return CALAOS_DISPLAY_BACKEND_X11;
    if (backend == "gles") return CALAOS_DISPLAY_BACKEND_GLES;

    return CALAOS_DISPLAY_BACKEND_NONE;
}

void DisplayBackendSelector::setBackendOverride(const std::string& backendName)
{
    if (backendName == "fbdev") backendOverride = CALAOS_DISPLAY_BACKEND_FBDEV;
    else if (backendName == "drm") backendOverride = CALAOS_DISPLAY_BACKEND_DRM;
    else if (backendName == "sdl") backendOverride = CALAOS_DISPLAY_BACKEND_SDL;
    else if (backendName == "x11") backendOverride = CALAOS_DISPLAY_BACKEND_X11;
    else if (backendName == "gles") backendOverride = CALAOS_DISPLAY_BACKEND_GLES;
    else backendOverride = CALAOS_DISPLAY_BACKEND_NONE;
}

void DisplayBackendSelector::setBackendOverride(calaos_display_backend_t backend)
{
    backendOverride = backend;
}

std::string DisplayBackendSelector::getBackendName(calaos_display_backend_t backend)
{
    switch (backend)
    {
        case CALAOS_DISPLAY_BACKEND_FBDEV: return "fbdev";
        case CALAOS_DISPLAY_BACKEND_DRM: return "drm";
        case CALAOS_DISPLAY_BACKEND_SDL: return "sdl";
        case CALAOS_DISPLAY_BACKEND_X11: return "x11";
        case CALAOS_DISPLAY_BACKEND_GLES: return "gles";
        default: return "none";
    }
}

bool DisplayBackendSelector::isBackendAvailable(calaos_display_backend_t backend)
{
    switch (backend)
    {
        case CALAOS_DISPLAY_BACKEND_FBDEV:
            return checkFbdevAvailable();
        case CALAOS_DISPLAY_BACKEND_DRM:
            return checkDrmAvailable();
        case CALAOS_DISPLAY_BACKEND_SDL:
            return checkSdlAvailable();
        case CALAOS_DISPLAY_BACKEND_X11:
            return checkX11Available();
        case CALAOS_DISPLAY_BACKEND_GLES:
            return checkGlfw3Available();
        default:
            return false;
    }
}

void DisplayBackendSelector::listAvailableBackends()
{
    std::cout << "Available display backends:" << std::endl;

    auto backends = getAvailableBackends();
    if (backends.empty())
    {
        std::cout << "  None" << std::endl;
        return;
    }

    for (auto backend : backends)
    {
        std::cout << "  - " << getBackendName(backend) << std::endl;
    }
}

bool DisplayBackendSelector::checkFbdevAvailable()
{
#if LV_USE_LINUX_FBDEV
    return access("/dev/fb0", F_OK) == 0;
#else
    return false;
#endif
}

bool DisplayBackendSelector::checkDrmAvailable()
{
#if LV_USE_LINUX_DRM
    return access("/dev/dri/card0", F_OK) == 0;
#else
    return false;
#endif
}

bool DisplayBackendSelector::checkSdlAvailable()
{
#if LV_USE_SDL
    // Check if we can initialize SDL (basic check)
    const char* display = std::getenv("DISPLAY");
    if (!display && !std::getenv("WAYLAND_DISPLAY"))
    {
        // No display environment for SDL
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool DisplayBackendSelector::checkX11Available()
{
#if LV_USE_X11
    const char* display = std::getenv("DISPLAY");
    return display != nullptr;
#else
    return false;
#endif
}

bool DisplayBackendSelector::checkGlfw3Available()
{
#if LV_USE_OPENGLES
    const char* display = std::getenv("DISPLAY");
    if (!display && !std::getenv("WAYLAND_DISPLAY"))
    {
        return false;
    }
    return true;
#else
    return false;
#endif
}

std::vector<calaos_display_backend_t> DisplayBackendSelector::getLinuxPriority()
{
    // Unified Linux priority: SDL > X11 > DRM > fbdev > glfw3
    return {
        CALAOS_DISPLAY_BACKEND_SDL,
        CALAOS_DISPLAY_BACKEND_X11,
        CALAOS_DISPLAY_BACKEND_DRM,
        CALAOS_DISPLAY_BACKEND_FBDEV,
        CALAOS_DISPLAY_BACKEND_GLES
    };
}

#endif // ESP_PLATFORM
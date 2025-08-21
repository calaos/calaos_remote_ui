#include "linux_hal_display.h"
#include "logging.h"
#include "lv_conf_platform.h"
#include <iostream>

// Include headers based on enabled backends
#if LV_USE_LINUX_FBDEV
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#endif

#if LV_USE_SDL
#include <SDL2/SDL.h>
#endif

#if LV_USE_X11
#include <X11/Xlib.h>
#endif

#include <chrono>
#include <cstdlib>

static const char* TAG = "hal.display";

static void linuxFbFlush(lv_display_t* disp, const lv_area_t* area, lv_color_t* colorP)
{
    LinuxHalDisplay* display = static_cast<LinuxHalDisplay*>(lv_display_get_user_data(disp));
    if (!display)
        return;
}

HalResult LinuxHalDisplay::init()
{
    ESP_LOGI(TAG, "Initializing Linux display");

    // Detect the best backend
    DisplayBackendSelector& selector = DisplayBackendSelector::getInstance();

    currentBackend = selector.detectBestBackend();

    if (currentBackend == CALAOS_DISPLAY_BACKEND_NONE)
    {
        ESP_LOGE(TAG, "No suitable display backend found");
        return HalResult::ERROR;
    }

    ESP_LOGI(TAG, "Using display backend: %s", selector.getBackendName(currentBackend).c_str());

    // Initialize the selected backend
    HalResult result = HalResult::ERROR;
    switch (currentBackend)
    {
        case CALAOS_DISPLAY_BACKEND_FBDEV:
            result = initFbdevBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_DRM:
            result = initDrmBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_SDL:
            result = initSdlBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_X11:
            result = initX11Backend();
            break;
        case CALAOS_DISPLAY_BACKEND_GLES:
            result = initGlfw3Backend();
            break;
        default:
            ESP_LOGE(TAG, "Unsupported backend: %s", selector.getBackendName(currentBackend).c_str());
            break;
    }

    if (result == HalResult::OK)
    {
        ESP_LOGI(TAG, "Linux display initialized successfully with %s backend",
                 selector.getBackendName(currentBackend).c_str());
    }

    return result;
}

HalResult LinuxHalDisplay::deinit()
{
    // Backend-specific cleanup
    switch (currentBackend)
    {
        case CALAOS_DISPLAY_BACKEND_FBDEV:
            deinitFbdevBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_DRM:
            deinitDrmBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_SDL:
            deinitSdlBackend();
            break;
        case CALAOS_DISPLAY_BACKEND_X11:
            deinitX11Backend();
            break;
        case CALAOS_DISPLAY_BACKEND_GLES:
            deinitGlfw3Backend();
            break;
        default:
            break;
    }

    // Common cleanup
    if (display)
    {
        lv_display_delete(display);
        display = nullptr;
    }

    currentBackend = CALAOS_DISPLAY_BACKEND_NONE;
    return HalResult::OK;
}

DisplayInfo LinuxHalDisplay::getDisplayInfo() const
{
    return displayInfo;
}

HalResult LinuxHalDisplay::setBacklight(uint8_t brightness)
{
    return HalResult::OK;
}

HalResult LinuxHalDisplay::backlightOn()
{
    return HalResult::OK;
}

HalResult LinuxHalDisplay::backlightOff()
{
    return HalResult::OK;
}

void LinuxHalDisplay::lock(uint32_t timeoutMs)
{
    if (timeoutMs == 0)
        displayMutex.lock();
    else
    {
        auto timeout = std::chrono::milliseconds(timeoutMs);
        if (!displayMutex.try_lock_for(timeout))
        {
            ESP_LOGW(TAG, "Failed to acquire display lock within timeout");
        }
    }
}

void LinuxHalDisplay::unlock()
{
    displayMutex.unlock();
}

lv_display_t* LinuxHalDisplay::getLvglDisplay()
{
    return display;
}

void LinuxHalDisplay::setBackendOverride(const std::string& backend)
{
    DisplayBackendSelector::getInstance().setBackendOverride(backend);
}

std::string LinuxHalDisplay::getCurrentBackend() const
{
    return DisplayBackendSelector::getInstance().getBackendName(currentBackend);
}

HalResult LinuxHalDisplay::initFbdevBackend()
{
#if LV_USE_LINUX_FBDEV
    ESP_LOGI(TAG, "Initializing framebuffer backend");

    const char* fbDevice = getenv("LV_LINUX_FBDEV_DEVICE");
    if (!fbDevice) fbDevice = "/dev/fb0";

    fbFd = open(fbDevice, O_RDWR);
    if (fbFd == -1)
    {
        ESP_LOGE(TAG, "Failed to open framebuffer device: %s", fbDevice);
        return HalResult::ERROR;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &vinfo) == -1)
    {
        ESP_LOGE(TAG, "Failed to get variable screen info");
        close(fbFd);
        return HalResult::ERROR;
    }

    displayInfo.width = 720;  // Fixed size for now
    displayInfo.height = 720;
    displayInfo.colorDepth = 16;

    display = lv_linux_fbdev_create();
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to create fbdev display");
        close(fbFd);
        return HalResult::ERROR;
    }

    lv_linux_fbdev_set_file(display, fbDevice);

    return HalResult::OK;
#else
    ESP_LOGE(TAG, "Framebuffer backend not compiled in");
    return HalResult::ERROR;
#endif
}

HalResult LinuxHalDisplay::initDrmBackend()
{
#if LV_USE_LINUX_DRM
    ESP_LOGI(TAG, "Initializing DRM backend");

    const char* drmCard = getenv("LV_LINUX_DRM_CARD");
    if (!drmCard) drmCard = "/dev/dri/card0";

    display = lv_linux_drm_create();
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to create DRM display");
        return HalResult::ERROR;
    }

    lv_linux_drm_set_file(display, drmCard, -1);

    displayInfo.width = 720;  // Will be updated by DRM driver
    displayInfo.height = 720;
    displayInfo.colorDepth = 16;

    return HalResult::OK;
#else
    ESP_LOGE(TAG, "DRM backend not compiled in");
    return HalResult::ERROR;
#endif
}

HalResult LinuxHalDisplay::initSdlBackend()
{
#if LV_USE_SDL
    ESP_LOGI(TAG, "Initializing SDL backend");

    display = lv_sdl_window_create(720, 720);
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to create SDL window");
        return HalResult::ERROR;
    }

    displayInfo.width = 720;
    displayInfo.height = 720;
    displayInfo.colorDepth = 32;  // SDL usually uses 32-bit

    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, display);
    lv_display_set_default(display);

    lv_indev_t *mousewheel = lv_sdl_mousewheel_create();
    lv_indev_set_display(mousewheel, display);

    lv_indev_t *kb = lv_sdl_keyboard_create();
    lv_indev_set_display(kb, display);

    return HalResult::OK;
#else
    ESP_LOGE(TAG, "SDL backend not compiled in");
    return HalResult::ERROR;
#endif
}

HalResult LinuxHalDisplay::initX11Backend()
{
#if LV_USE_X11
    ESP_LOGI(TAG, "Initializing X11 backend");

    display = lv_x11_window_create("Calaos Remote UI", 720, 720);
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to create X11 window");
        return HalResult::ERROR;
    }

    displayInfo.width = 720;
    displayInfo.height = 720;
    displayInfo.colorDepth = 32;  // X11 usually uses 32-bit

    // add default x11 input device
    lv_x11_inputs_create(display, nullptr);

    return HalResult::OK;
#else
    ESP_LOGE(TAG, "X11 backend not compiled in");
    return HalResult::ERROR;
#endif
}

HalResult LinuxHalDisplay::initGlfw3Backend()
{
#if LV_USE_OPENGLES
    ESP_LOGI(TAG, "Initializing GLES backend");

    lv_glfw_texture_t *window_texture;
    lv_indev_t *mouse;
    lv_display_t *disp_texture;
    uint32_t disp_texture_id;

    displayInfo.width = 720;
    displayInfo.height = 720;
    displayInfo.colorDepth = 32;

    lv_glfw_window_t *window = lv_glfw_window_create(
            displayInfo.width, displayInfo.height, true);

    /* create a display that flushes to a texture */
    disp_texture = lv_opengles_texture_create(
            displayInfo.width, displayInfo.height);
    lv_display_set_default(disp_texture);

    /* add the texture to the window */
    disp_texture_id = lv_opengles_texture_get_texture_id(disp_texture);
    window_texture = lv_glfw_window_add_texture(window, disp_texture_id,
            displayInfo.width, displayInfo.height);

    /* get the mouse indev of the window texture */
    mouse = lv_glfw_texture_get_mouse_indev(window_texture);

    return HalResult::OK;
#else
    ESP_LOGE(TAG, "GLES backend not compiled in");
    return HalResult::ERROR;
#endif
}

void LinuxHalDisplay::deinitFbdevBackend()
{
#if LV_USE_LINUX_FBDEV
    if (fbFd != -1)
    {
        close(fbFd);
        fbFd = -1;
    }
#endif
}

void LinuxHalDisplay::deinitDrmBackend()
{
    // DRM cleanup is handled by LVGL
}

void LinuxHalDisplay::deinitSdlBackend()
{
    // SDL cleanup is handled by LVGL
}

void LinuxHalDisplay::deinitX11Backend()
{
    // X11 cleanup is handled by LVGL
}

void LinuxHalDisplay::deinitGlfw3Backend()
{
    // GLFW3 cleanup would be handled here
}
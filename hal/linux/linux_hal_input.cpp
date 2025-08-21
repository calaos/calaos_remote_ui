#include "linux_hal_input.h"
#include "logging.h"
#include <cstdlib>

static const char* TAG = "hal.input";

HalResult LinuxHalInput::init()
{
    ESP_LOGI(TAG, "Initializing Linux input");
    
    // Detect the best input backend
    currentInputBackend = detectBestInputBackend();
    
    if (currentInputBackend == CALAOS_INPUT_BACKEND_NONE)
    {
        ESP_LOGI(TAG, "No separate input backend found - assuming display backend handles input automatically");
        inputDevice = nullptr;
        return HalResult::OK;
    }
    
    ESP_LOGI(TAG, "Using input backend: %s", getInputBackendName(currentInputBackend).c_str());
    
    // Initialize the selected backend
    HalResult result = HalResult::ERROR;
    switch (currentInputBackend)
    {
        case CALAOS_INPUT_BACKEND_EVDEV:
            result = initEvdevBackend();
            break;
        case CALAOS_INPUT_BACKEND_LIBINPUT:
            result = initLibinputBackend();
            break;
        default:
            ESP_LOGE(TAG, "Unsupported input backend: %s", getInputBackendName(currentInputBackend).c_str());
            break;
    }
    
    if (result == HalResult::OK)
    {
        ESP_LOGI(TAG, "Linux input initialized successfully with %s backend", 
                 getInputBackendName(currentInputBackend).c_str());
    }
    
    return result;
}

HalResult LinuxHalInput::deinit()
{
    // LVGL handles cleanup automatically
    if (inputDevice)
    {
        lv_indev_delete(inputDevice);
        inputDevice = nullptr;
    }

    currentInputBackend = CALAOS_INPUT_BACKEND_NONE;
    return HalResult::OK;
}

lv_indev_t* LinuxHalInput::getLvglInputDevice()
{
    return inputDevice;
}


void LinuxHalInput::setInputBackendOverride(const std::string& backend)
{
    if (backend == "evdev") backendOverride = CALAOS_INPUT_BACKEND_EVDEV;
    else if (backend == "libinput") backendOverride = CALAOS_INPUT_BACKEND_LIBINPUT;
    else backendOverride = CALAOS_INPUT_BACKEND_NONE;
}

std::string LinuxHalInput::getCurrentInputBackend() const
{
    return getInputBackendName(currentInputBackend);
}

calaos_input_backend_t LinuxHalInput::detectBestInputBackend()
{
    // Check for environment variable override first
    calaos_input_backend_t envBackend = getInputBackendFromEnv();
    if (envBackend != CALAOS_INPUT_BACKEND_NONE)
    {
        if (isInputBackendAvailable(envBackend))
        {
            ESP_LOGI(TAG, "Using input backend from environment: %s", getInputBackendName(envBackend).c_str());
            return envBackend;
        }
        else
        {
            ESP_LOGW(TAG, "Requested input backend %s not available, falling back to auto-detection", 
                     getInputBackendName(envBackend).c_str());
        }
    }
    
    // Check for override
    if (backendOverride != CALAOS_INPUT_BACKEND_NONE && isInputBackendAvailable(backendOverride))
    {
        ESP_LOGI(TAG, "Using override input backend: %s", getInputBackendName(backendOverride).c_str());
        return backendOverride;
    }
    
    // Priority order for Linux input: evdev > libinput
    std::vector<calaos_input_backend_t> priorities = {
        CALAOS_INPUT_BACKEND_EVDEV,
        CALAOS_INPUT_BACKEND_LIBINPUT
    };
    
    // Find first available backend
    for (auto backend : priorities)
    {
        if (isInputBackendAvailable(backend))
        {
            ESP_LOGI(TAG, "Selected input backend: %s", getInputBackendName(backend).c_str());
            return backend;
        }
    }
    
    ESP_LOGD(TAG, "No separate input backend available - display backend may handle input");
    return CALAOS_INPUT_BACKEND_NONE;
}

bool LinuxHalInput::isInputBackendAvailable(calaos_input_backend_t backend)
{
    switch (backend)
    {
        case CALAOS_INPUT_BACKEND_EVDEV:
#if LV_USE_LINUX_EVDEV
            return access("/dev/input", F_OK) == 0;
#else
            return false;
#endif
        case CALAOS_INPUT_BACKEND_LIBINPUT:
#if LV_USE_LINUX_LIBINPUT
            return access("/dev/input", F_OK) == 0;  // Simplified check
#else
            return false;
#endif
        default:
            return false;
    }
}

std::string LinuxHalInput::getInputBackendName(calaos_input_backend_t backend) const
{
    switch (backend)
    {
        case CALAOS_INPUT_BACKEND_EVDEV: return "evdev";
        case CALAOS_INPUT_BACKEND_LIBINPUT: return "libinput";
        default: return "none";
    }
}

calaos_input_backend_t LinuxHalInput::getInputBackendFromEnv()
{
    const char* envVar = std::getenv("CALAOS_INPUT_BACKEND");
    if (!envVar) return CALAOS_INPUT_BACKEND_NONE;
    
    std::string backend(envVar);
    
    if (backend == "evdev") return CALAOS_INPUT_BACKEND_EVDEV;
    if (backend == "libinput") return CALAOS_INPUT_BACKEND_LIBINPUT;
    
    return CALAOS_INPUT_BACKEND_NONE;
}

HalResult LinuxHalInput::initEvdevBackend()
{
#if LV_USE_LINUX_EVDEV
    ESP_LOGI(TAG, "Initializing evdev input backend");
    
    // Use environment variable override if set
    const char* evdevDevice = getenv("LV_LINUX_EVDEV_POINTER_DEVICE");
    if (!evdevDevice) evdevDevice = "/dev/input/event*";  // Let LVGL find the device
    
    inputDevice = lv_linux_evdev_create(LV_INDEV_TYPE_POINTER, evdevDevice);
    if (!inputDevice)
    {
        ESP_LOGE(TAG, "Failed to create evdev input device");
        return HalResult::ERROR;
    }
    
    ESP_LOGI(TAG, "evdev input device created successfully");
    return HalResult::OK;
#else
    ESP_LOGE(TAG, "evdev backend not compiled in");
    return HalResult::ERROR;
#endif
}

HalResult LinuxHalInput::initLibinputBackend()
{
#if LV_USE_LINUX_LIBINPUT
    ESP_LOGI(TAG, "Initializing libinput backend");
    
    inputDevice = lv_linux_libinput_create(LV_INDEV_TYPE_POINTER, "/dev/input/event*");
    if (!inputDevice)
    {
        ESP_LOGE(TAG, "Failed to create libinput input device");
        return HalResult::ERROR;
    }
    
    ESP_LOGI(TAG, "libinput input device created successfully");
    return HalResult::OK;
#else
    ESP_LOGE(TAG, "libinput backend not compiled in");
    return HalResult::ERROR;
#endif
}
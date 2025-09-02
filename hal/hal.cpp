#include "hal.h"

#ifdef ESP_PLATFORM
#include "esp32/esp32_hal.h"
#else
#include "linux/linux_hal.h"
#endif

#include "logging.h"

HAL& HAL::getInstance()
{
#ifdef ESP_PLATFORM
    return Esp32HAL::getInstance();
#else
    return LinuxHAL::getInstance();
#endif
}

HAL::HAL()
{
    initLogger();
}

void HAL::initLogger()
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AppStore", ESP_LOG_VERBOSE);
    esp_log_level_set("StartupPage", ESP_LOG_VERBOSE);
    esp_log_level_set("hal.network", ESP_LOG_VERBOSE);
}

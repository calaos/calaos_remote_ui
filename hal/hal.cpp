#include "hal.h"

#ifdef ESP_PLATFORM
#include "esp32/esp32_hal.h"
#else
#include "linux/linux_hal.h"
#endif

HAL& HAL::getInstance()
{
#ifdef ESP_PLATFORM
    return Esp32HAL::getInstance();
#else
    return LinuxHAL::getInstance();
#endif
}
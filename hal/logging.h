#pragma once

#ifdef ESP_PLATFORM
    // ESP32 platform - use ESP-IDF logging
    #include "esp_log.h"
#else
    // Linux platform - include the Linux-specific logging
    #include "linux/logging.h"
#endif
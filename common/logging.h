#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <iostream>
#include <cstdio>

// Define ESP_LOG levels for Linux platform
#define ESP_LOG_NONE    (0)
#define ESP_LOG_ERROR   (1)
#define ESP_LOG_WARN    (2)
#define ESP_LOG_INFO    (3)
#define ESP_LOG_DEBUG   (4)
#define ESP_LOG_VERBOSE (5)

// ANSI color codes for different log levels
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_YELLOW  "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_MAGENTA "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR_WHITE   "37"

#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"

// ESP_LOG macros for Linux platform
#define ESP_LOGE(tag, format, ...) \
    printf(LOG_COLOR(LOG_COLOR_RED) "E (%lu) %s: " format LOG_RESET_COLOR "\n", \
           (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
               std::chrono::steady_clock::now().time_since_epoch()).count()), \
           tag, ##__VA_ARGS__)

#define ESP_LOGW(tag, format, ...) \
    printf(LOG_COLOR(LOG_COLOR_YELLOW) "W (%lu) %s: " format LOG_RESET_COLOR "\n", \
           (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
               std::chrono::steady_clock::now().time_since_epoch()).count()), \
           tag, ##__VA_ARGS__)

#define ESP_LOGI(tag, format, ...) \
    printf(LOG_COLOR(LOG_COLOR_GREEN) "I (%lu) %s: " format LOG_RESET_COLOR "\n", \
           (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
               std::chrono::steady_clock::now().time_since_epoch()).count()), \
           tag, ##__VA_ARGS__)

#define ESP_LOGD(tag, format, ...) \
    printf(LOG_COLOR(LOG_COLOR_CYAN) "D (%lu) %s: " format LOG_RESET_COLOR "\n", \
           (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
               std::chrono::steady_clock::now().time_since_epoch()).count()), \
           tag, ##__VA_ARGS__)

#define ESP_LOGV(tag, format, ...) \
    printf("V (%lu) %s: " format "\n", \
           (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
               std::chrono::steady_clock::now().time_since_epoch()).count()), \
           tag, ##__VA_ARGS__)

// Include chrono for timestamp
#include <chrono>

#endif // ESP_PLATFORM
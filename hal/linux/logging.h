#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <iostream>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <mutex>

// Forward declaration
typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

// Define ESP_LOG levels for Linux platform  
#define ESP_LOG_NONE    ((esp_log_level_t)0)
#define ESP_LOG_ERROR   ((esp_log_level_t)1)
#define ESP_LOG_WARN    ((esp_log_level_t)2)
#define ESP_LOG_INFO    ((esp_log_level_t)3)
#define ESP_LOG_DEBUG   ((esp_log_level_t)4)
#define ESP_LOG_VERBOSE ((esp_log_level_t)5)

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

// Linux logging implementation
class LinuxLogger
{
public:
    static LinuxLogger& getInstance();
    void setLogLevel(const std::string& tag, esp_log_level_t level);
    esp_log_level_t getLogLevel(const std::string& tag) const;
    bool shouldLog(const std::string& tag, esp_log_level_t level) const;

private:
    LinuxLogger();
    mutable std::mutex mutex_;
    std::unordered_map<std::string, esp_log_level_t> tagLevels_;
    esp_log_level_t globalLevel_ = ESP_LOG_INFO;
};

// Function declarations
void esp_log_level_set(const char* tag, esp_log_level_t level);

// ESP_LOG macros for Linux platform with level checking
#define ESP_LOGE(tag, format, ...) \
    do { \
        if (LinuxLogger::getInstance().shouldLog(tag, ESP_LOG_ERROR)) { \
            printf(LOG_COLOR(LOG_COLOR_RED) "E (%lu) %s: " format LOG_RESET_COLOR "\n", \
                   (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
                       std::chrono::steady_clock::now().time_since_epoch()).count()), \
                   tag, ##__VA_ARGS__); \
        } \
    } while(0)

#define ESP_LOGW(tag, format, ...) \
    do { \
        if (LinuxLogger::getInstance().shouldLog(tag, ESP_LOG_WARN)) { \
            printf(LOG_COLOR(LOG_COLOR_YELLOW) "W (%lu) %s: " format LOG_RESET_COLOR "\n", \
                   (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
                       std::chrono::steady_clock::now().time_since_epoch()).count()), \
                   tag, ##__VA_ARGS__); \
        } \
    } while(0)

#define ESP_LOGI(tag, format, ...) \
    do { \
        if (LinuxLogger::getInstance().shouldLog(tag, ESP_LOG_INFO)) { \
            printf(LOG_COLOR(LOG_COLOR_GREEN) "I (%lu) %s: " format LOG_RESET_COLOR "\n", \
                   (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
                       std::chrono::steady_clock::now().time_since_epoch()).count()), \
                   tag, ##__VA_ARGS__); \
        } \
    } while(0)

#define ESP_LOGD(tag, format, ...) \
    do { \
        if (LinuxLogger::getInstance().shouldLog(tag, ESP_LOG_DEBUG)) { \
            printf(LOG_COLOR(LOG_COLOR_CYAN) "D (%lu) %s: " format LOG_RESET_COLOR "\n", \
                   (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
                       std::chrono::steady_clock::now().time_since_epoch()).count()), \
                   tag, ##__VA_ARGS__); \
        } \
    } while(0)

#define ESP_LOGV(tag, format, ...) \
    do { \
        if (LinuxLogger::getInstance().shouldLog(tag, ESP_LOG_VERBOSE)) { \
            printf("V (%lu) %s: " format "\n", \
                   (unsigned long)(std::chrono::duration_cast<std::chrono::milliseconds>( \
                       std::chrono::steady_clock::now().time_since_epoch()).count()), \
                   tag, ##__VA_ARGS__); \
        } \
    } while(0)

// Include chrono for timestamp
#include <chrono>

#endif // ESP_PLATFORM
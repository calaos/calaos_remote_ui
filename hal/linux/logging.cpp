#include "logging.h"

#ifndef ESP_PLATFORM

LinuxLogger& LinuxLogger::getInstance()
{
    static LinuxLogger instance;
    return instance;
}

LinuxLogger::LinuxLogger()
{
    // Default global level is INFO
    globalLevel_ = ESP_LOG_INFO;
}

void LinuxLogger::setLogLevel(const std::string& tag, esp_log_level_t level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (tag == "*")
    {
        // Set global level for all tags
        globalLevel_ = level;
        // Clear all specific tag levels so they use the global level
        tagLevels_.clear();
    }
    else
    {
        // Set level for specific tag
        tagLevels_[tag] = level;
    }
}

esp_log_level_t LinuxLogger::getLogLevel(const std::string& tag) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if tag has specific level set
    auto it = tagLevels_.find(tag);
    if (it != tagLevels_.end())
    {
        return it->second;
    }
    
    // Use global level
    return globalLevel_;
}

bool LinuxLogger::shouldLog(const std::string& tag, esp_log_level_t level) const
{
    esp_log_level_t tagLevel = getLogLevel(tag);
    return level <= tagLevel;
}

// C function implementation compatible with ESP-IDF
void esp_log_level_set(const char* tag, esp_log_level_t level)
{
    std::string tagStr = tag ? tag : "*";
    LinuxLogger::getInstance().setLogLevel(tagStr, level);
}

#endif // ESP_PLATFORM
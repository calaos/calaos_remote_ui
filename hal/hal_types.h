#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>
#include <functional>

enum class HalResult
{
    OK = 0,
    ERROR = -1,
    TIMEOUT = -2,
    BUSY = -3,
    NOT_SUPPORTED = -4
};

struct DisplayInfo
{
    uint16_t width;
    uint16_t height;
    uint8_t colorDepth;
};

struct WifiConfig
{
    std::string ssid;
    std::string password;
    uint8_t bssid[6];
    int8_t rssi;
};

enum class WifiStatus
{
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED,
    ERROR
};

using WifiEventCallback = std::function<void(WifiStatus status)>;
#pragma once

#include <stdint.h>
#include <string>
#include <functional>
#include <vector>
#include <memory>

enum class NetworkResult
{
    OK = 0,
    ERROR = -1,
    TIMEOUT = -2,
    CONNECTION_FAILED = -3,
    INVALID_PARAMETER = -4,
    BUFFER_TOO_SMALL = -5,
    NOT_INITIALIZED = -6,
    ALREADY_CONNECTED = -7,
    NOT_CONNECTED = -8
};

enum class NetworkProtocol
{
    UDP,
    TCP,
    HTTP,
    HTTPS,
    WS,
    WSS
};

struct NetworkAddress
{
    std::string host;
    uint16_t port;
    
    NetworkAddress() : port(0) {}
    NetworkAddress(const std::string& h, uint16_t p) : host(h), port(p) {}
};

struct NetworkBuffer
{
    std::vector<uint8_t> data;
    size_t size;
    
    NetworkBuffer() : size(0) {}
    NetworkBuffer(const void* ptr, size_t len) : data((uint8_t*)ptr, (uint8_t*)ptr + len), size(len) {}
    NetworkBuffer(const std::string& str) : data(str.begin(), str.end()), size(str.size()) {}
};

using NetworkCallback = std::function<void(NetworkResult result, const NetworkBuffer& data)>;
using NetworkConnectionCallback = std::function<void(NetworkResult result)>;
using NetworkErrorCallback = std::function<void(NetworkResult error, const std::string& message)>;
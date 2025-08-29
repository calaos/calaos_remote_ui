#pragma once

#include "../network_types.h"
#include <atomic>
#include <thread>
#include <mutex>

class UdpClient
{
public:
    UdpClient();
    ~UdpClient();

    NetworkResult init();
    void cleanup();
    
    NetworkResult sendTo(const NetworkAddress& address, const NetworkBuffer& data);
    NetworkResult sendBroadcast(uint16_t port, const NetworkBuffer& data);
    
    NetworkResult startReceiving(uint16_t port, NetworkCallback callback);
    void stopReceiving();
    
    bool isReceiving() const;
    
    void setReceiveTimeout(uint32_t timeoutMs);
    void setErrorCallback(NetworkErrorCallback callback);

private:
    void receiveThread();
    NetworkResult createSocket();
    void closeSocket();
    
    int socket_;
    std::atomic<bool> receiving_;
    std::thread receive_thread_;
    std::mutex socket_mutex_;
    
    uint16_t listen_port_;
    uint32_t receive_timeout_ms_;
    
    NetworkCallback receive_callback_;
    NetworkErrorCallback error_callback_;
};
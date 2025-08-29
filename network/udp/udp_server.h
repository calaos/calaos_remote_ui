#pragma once

#include "network_types.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

struct UdpClientInfo
{
    NetworkAddress address;
    uint64_t last_seen;

    UdpClientInfo() : last_seen(0) {}
    UdpClientInfo(const NetworkAddress& addr, uint64_t timestamp)
        : address(addr), last_seen(timestamp) {}
};

class UdpServer
{
public:
    UdpServer();
    ~UdpServer();

    NetworkResult init();
    void cleanup();

    NetworkResult startListening(uint16_t port, NetworkCallback callback);
    void stopListening();

    NetworkResult sendTo(const NetworkAddress& address, const NetworkBuffer& data);
    NetworkResult sendBroadcast(const NetworkBuffer& data);
    NetworkResult sendToAllClients(const NetworkBuffer& data);

    bool isListening() const;
    std::vector<UdpClientInfo> getConnectedClients() const;

    void setClientTimeout(uint32_t timeoutMs);
    void setErrorCallback(NetworkErrorCallback callback);

private:
    void listenThread();
    void cleanupExpiredClients();
    NetworkResult createSocket();
    void closeSocket();
    void updateClientList(const NetworkAddress& client_addr);

    int socket_;
    std::atomic<bool> listening_;
    std::thread listen_thread_;
    mutable std::mutex socket_mutex_;
    mutable std::mutex clients_mutex_;

    uint16_t listen_port_;
    uint32_t client_timeout_ms_;

    std::vector<UdpClientInfo> connected_clients_;
    NetworkCallback receive_callback_;
    NetworkErrorCallback error_callback_;
};
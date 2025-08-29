#pragma once

#include "network_types.h"
#include "udp/udp_client.h"
#include "udp/udp_server.h"
#include "http/http_client.h"
#include "websocket/websocket_client.h"
#include <memory>
#include <mutex>

class CalaosNet
{
public:
    static CalaosNet& instance();
    
    UdpClient& udpClient();
    UdpServer& udpServer();
    HttpClient& httpClient();
    WebSocketClient& webSocketClient();
    
    NetworkResult init();
    void cleanup();
    
    bool isInitialized() const;
    
    void setGlobalErrorCallback(NetworkErrorCallback callback);

private:
    CalaosNet();
    ~CalaosNet();
    
    CalaosNet(const CalaosNet&) = delete;
    CalaosNet& operator=(const CalaosNet&) = delete;
    CalaosNet(CalaosNet&&) = delete;
    CalaosNet& operator=(CalaosNet&&) = delete;
    
    void globalErrorHandler(NetworkResult error, const std::string& message);
    
    mutable std::mutex init_mutex_;
    bool initialized_;
    
    std::unique_ptr<UdpClient> udp_client_;
    std::unique_ptr<UdpServer> udp_server_;
    std::unique_ptr<HttpClient> http_client_;
    std::unique_ptr<WebSocketClient> websocket_client_;
    
    NetworkErrorCallback global_error_callback_;
};
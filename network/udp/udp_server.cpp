#include "udp_server.h"
#include "logging.h"

#include <string.h>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#elif defined(ESP_PLATFORM)
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_timer.h"
#endif

static const char *TAG = "net.udp";

static uint64_t getCurrentTimestamp()
{
#ifdef __linux__
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#elif defined(ESP_PLATFORM)
    return esp_timer_get_time() / 1000;
#endif
}

UdpServer::UdpServer():
    socket_(-1),
    listening_(false),
    listen_port_(0),
    client_timeout_ms_(30000)
{
}

UdpServer::~UdpServer()
{
    cleanup();
}

NetworkResult UdpServer::init()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ != -1)
    {
        ESP_LOGW(TAG, "UDP server already initialized");
        return NetworkResult::ALREADY_CONNECTED;
    }

    return createSocket();
}

void UdpServer::cleanup()
{
    stopListening();
    closeSocket();
}

NetworkResult UdpServer::createSocket()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP server socket: %s", strerror(errno));
        return NetworkResult::ERROR;
    }

    int broadcast = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
        ESP_LOGW(TAG, "Failed to enable broadcast on UDP server socket: %s", strerror(errno));
    }

    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR on UDP server socket: %s", strerror(errno));
    }

    ESP_LOGI(TAG, "UDP server initialized successfully");
    return NetworkResult::OK;
}

void UdpServer::closeSocket()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ != -1)
    {
        close(socket_);
        socket_ = -1;
        ESP_LOGI(TAG, "UDP server socket closed");
    }
}

NetworkResult UdpServer::startListening(uint16_t port, NetworkCallback callback)
{
    if (listening_.load())
    {
        ESP_LOGE(TAG, "UDP server already listening");
        return NetworkResult::ALREADY_CONNECTED;
    }

    if (!callback)
    {
        ESP_LOGE(TAG, "Invalid callback provided");
        return NetworkResult::INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ == -1)
    {
        NetworkResult result = createSocket();
        if (result != NetworkResult::OK)
        {
            return result;
        }
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Failed to bind UDP server socket to port %d: %s", port, strerror(errno));
        return NetworkResult::ERROR;
    }

    listen_port_ = port;
    receive_callback_ = callback;
    listening_.store(true);

    listen_thread_ = std::thread(&UdpServer::listenThread, this);

    ESP_LOGI(TAG, "Started UDP server listening on port %d", port);
    return NetworkResult::OK;
}

void UdpServer::stopListening()
{
    if (listening_.load())
    {
        listening_.store(false);

        if (listen_thread_.joinable())
        {
            listen_thread_.join();
        }

        std::lock_guard<std::mutex> clients_lock(clients_mutex_);
        connected_clients_.clear();

        ESP_LOGI(TAG, "Stopped UDP server listening");
    }
}

bool UdpServer::isListening() const
{
    return listening_.load();
}

NetworkResult UdpServer::sendTo(const NetworkAddress& address, const NetworkBuffer& data)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ == -1)
    {
        ESP_LOGE(TAG, "UDP server not initialized");
        return NetworkResult::NOT_INITIALIZED;
    }

    if (address.host.empty() || address.port == 0)
    {
        ESP_LOGE(TAG, "Invalid address parameters");
        return NetworkResult::INVALID_PARAMETER;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(address.port);

    if (inet_pton(AF_INET, address.host.c_str(), &dest_addr.sin_addr) <= 0)
    {
        ESP_LOGE(TAG, "Invalid IP address: %s", address.host.c_str());
        return NetworkResult::INVALID_PARAMETER;
    }

    ssize_t bytes_sent = sendto(socket_, data.data.data(), data.size, 0,
                               (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (bytes_sent < 0)
    {
        ESP_LOGE(TAG, "Failed to send UDP data from server: %s", strerror(errno));
        return NetworkResult::ERROR;
    }

    if ((size_t)bytes_sent != data.size)
    {
        ESP_LOGW(TAG, "Partial UDP send from server: %zd/%zu bytes", bytes_sent, data.size);
    }

    ESP_LOGD(TAG, "Sent UDP packet from server to %s:%d (%zu bytes)",
              address.host.c_str(), address.port, data.size);

    return NetworkResult::OK;
}

NetworkResult UdpServer::sendBroadcast(const NetworkBuffer& data)
{
    NetworkAddress broadcast_addr("255.255.255.255", listen_port_);
    return sendTo(broadcast_addr, data);
}

NetworkResult UdpServer::sendToAllClients(const NetworkBuffer& data)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);

    NetworkResult result = NetworkResult::OK;
    size_t successful_sends = 0;

    for (const auto& client : connected_clients_)
    {
        NetworkResult send_result = sendTo(client.address, data);
        if (send_result == NetworkResult::OK)
        {
            successful_sends++;
        }
        else
        {
            result = send_result;
        }
    }

    ESP_LOGD(TAG, "Sent UDP data to %zu/%zu connected clients",
              successful_sends, connected_clients_.size());

    return (successful_sends > 0) ? NetworkResult::OK : result;
}

std::vector<UdpClientInfo> UdpServer::getConnectedClients() const
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return connected_clients_;
}

void UdpServer::setClientTimeout(uint32_t timeoutMs)
{
    client_timeout_ms_ = timeoutMs;
}

void UdpServer::setErrorCallback(NetworkErrorCallback callback)
{
    error_callback_ = callback;
}

void UdpServer::updateClientList(const NetworkAddress& client_addr)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);

    uint64_t current_time = getCurrentTimestamp();

    for (auto& client : connected_clients_)
    {
        if (client.address.host == client_addr.host && client.address.port == client_addr.port)
        {
            client.last_seen = current_time;
            return;
        }
    }

    connected_clients_.emplace_back(client_addr, current_time);
    ESP_LOGD(TAG, "New UDP client connected: %s:%d",
              client_addr.host.c_str(), client_addr.port);
}

void UdpServer::cleanupExpiredClients()
{
    std::lock_guard<std::mutex> lock(clients_mutex_);

    uint64_t current_time = getCurrentTimestamp();
    size_t initial_count = connected_clients_.size();

    connected_clients_.erase(
        std::remove_if(connected_clients_.begin(), connected_clients_.end(),
                      [this, current_time](const UdpClientInfo& client)
                      {
                          return (current_time - client.last_seen) > client_timeout_ms_;
                      }),
        connected_clients_.end());

    if (connected_clients_.size() != initial_count)
    {
        ESP_LOGD(TAG, "Cleaned up %zu expired UDP clients",
                  initial_count - connected_clients_.size());
    }
}

void UdpServer::listenThread()
{
    const size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while (listening_.load())
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (socket_ == -1)
            {
                break;
            }
            FD_SET(socket_, &read_fds);
        }

        int select_result = select(socket_ + 1, &read_fds, nullptr, nullptr, &timeout);

        if (!listening_.load())
        {
            break;
        }

        if (select_result < 0)
        {
            if (errno != EINTR)
            {
                ESP_LOGE(TAG, "Select failed in UDP server listen thread: %s", strerror(errno));
                if (error_callback_)
                {
                    error_callback_(NetworkResult::ERROR, "Select failed");
                }
            }
            continue;
        }

        if (select_result == 0)
        {
            cleanupExpiredClients();
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        ssize_t bytes_received;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (socket_ == -1)
            {
                break;
            }

            bytes_received = recvfrom(socket_, buffer.data(), BUFFER_SIZE, 0,
                                    (struct sockaddr*)&client_addr, &addr_len);
        }

        if (bytes_received < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGE(TAG, "Failed to receive UDP data in server: %s", strerror(errno));
                if (error_callback_)
                {
                    error_callback_(NetworkResult::ERROR, "Receive failed");
                }
            }
            continue;
        }

        if (bytes_received > 0)
        {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

            NetworkAddress sender_addr(client_ip, ntohs(client_addr.sin_port));
            updateClientList(sender_addr);

            if (receive_callback_)
            {
                NetworkBuffer received_data(buffer.data(), bytes_received);
                receive_callback_(NetworkResult::OK, received_data);
            }

            ESP_LOGD(TAG, "Received UDP packet in server from %s:%d (%zd bytes)",
                     client_ip, ntohs(client_addr.sin_port), bytes_received);
        }
    }

    ESP_LOGD(TAG, "UDP server listen thread terminated");
}
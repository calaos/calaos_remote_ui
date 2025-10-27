#include "udp_client.h"
#include "logging.h"

#include <string.h>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#elif defined(ESP_PLATFORM)
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#endif

static const char *TAG = "net.udp";

UdpClient::UdpClient():
    socket_(-1),
    receiving_(false),
    listen_port_(0),
    receive_timeout_ms_(5000)
{
}

UdpClient::~UdpClient()
{
    cleanup();
}

NetworkResult UdpClient::init()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ != -1)
    {
        ESP_LOGE(TAG, "UDP client already initialized");
        return NetworkResult::ALREADY_CONNECTED;
    }

    return createSocket();
}

void UdpClient::cleanup()
{
    stopReceiving();
    closeSocket();
}

NetworkResult UdpClient::createSocket()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP socket: %s", strerror(errno));
        return NetworkResult::ERROR;
    }

    int broadcast = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
        ESP_LOGW(TAG, "Failed to enable broadcast on UDP socket: %s", strerror(errno));
    }

    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR on UDP socket: %s", strerror(errno));
    }

    ESP_LOGI(TAG, "UDP client initialized successfully");
    return NetworkResult::OK;
}

void UdpClient::closeSocket()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ != -1)
    {
        close(socket_);
        socket_ = -1;
        ESP_LOGI(TAG, "UDP socket closed");
    }
}

NetworkResult UdpClient::sendTo(const NetworkAddress& address, const NetworkBuffer& data)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_ == -1)
    {
        ESP_LOGE(TAG, "UDP client not initialized");
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
        ESP_LOGE(TAG, "Failed to send UDP data: %s", strerror(errno));
        return NetworkResult::ERROR;
    }

    if ((size_t)bytes_sent != data.size)
    {
        ESP_LOGW(TAG, "Partial UDP send: %zd/%zu bytes", bytes_sent, data.size);
    }

    return NetworkResult::OK;
}

NetworkResult UdpClient::sendBroadcast(uint16_t port, const NetworkBuffer& data)
{
    NetworkAddress broadcast_addr("255.255.255.255", port);
    return sendTo(broadcast_addr, data);
}

NetworkResult UdpClient::startReceiving(uint16_t port, NetworkCallback callback)
{
    if (receiving_.load())
    {
        ESP_LOGE(TAG, "UDP client already receiving");
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

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(port);

    if (bind(socket_, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0)
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket to port %d: %s", port, strerror(errno));
        return NetworkResult::ERROR;
    }

    listen_port_ = port;
    receive_callback_ = callback;
    receiving_.store(true);

    receive_thread_ = std::thread(&UdpClient::receiveThread, this);

    ESP_LOGI(TAG, "Started UDP receiving on port %d", port);
    return NetworkResult::OK;
}

void UdpClient::stopReceiving()
{
    if (receiving_.load())
    {
        receiving_.store(false);

        if (receive_thread_.joinable())
        {
            receive_thread_.join();
        }

        ESP_LOGI(TAG, "Stopped UDP receiving");
    }
}

bool UdpClient::isReceiving() const
{
    return receiving_.load();
}

void UdpClient::setReceiveTimeout(uint32_t timeoutMs)
{
    receive_timeout_ms_ = timeoutMs;
}

void UdpClient::setErrorCallback(NetworkErrorCallback callback)
{
    error_callback_ = callback;
}

void UdpClient::receiveThread()
{
    const size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    while (receiving_.load())
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

        // Use a short timeout (100ms) to allow quick thread termination
        // select() can modify the timeval, so recreate it each iteration
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        int select_result = select(socket_ + 1, &read_fds, nullptr, nullptr, &timeout);

        if (!receiving_.load())
        {
            break;
        }

        if (select_result < 0)
        {
            if (errno != EINTR)
            {
                ESP_LOGE(TAG, "Select failed in UDP receive thread: %s", strerror(errno));
                if (error_callback_)
                {
                    error_callback_(NetworkResult::ERROR, "Select failed");
                }
            }
            continue;
        }

        if (select_result == 0)
        {
            continue;
        }

        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);

        ssize_t bytes_received;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (socket_ == -1)
            {
                break;
            }

            bytes_received = recvfrom(socket_, buffer.data(), BUFFER_SIZE, 0,
                                    (struct sockaddr*)&sender_addr, &addr_len);
        }

        if (bytes_received < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                ESP_LOGE(TAG, "Failed to receive UDP data: %s", strerror(errno));
                if (error_callback_)
                {
                    error_callback_(NetworkResult::ERROR, "Receive failed");
                }
            }
            continue;
        }

        if (bytes_received > 0 && receive_callback_)
        {
            NetworkBuffer received_data(buffer.data(), bytes_received);
            receive_callback_(NetworkResult::OK, received_data);

            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
        }
    }

    ESP_LOGD(TAG, "UDP receive thread terminated");
}
#include "linux_hal_network.h"
#include "logging.h"
#include "flux.h"
#include "hal.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <thread>
#include <chrono>

static const char* TAG = "hal.network";

LinuxHalNetwork::~LinuxHalNetwork()
{
    deinit();
}

HalResult LinuxHalNetwork::init()
{
    ESP_LOGI(TAG, "Initializing Linux network");

    // Start status monitoring thread
    thread_running_ = true;
    status_thread_ = std::thread(&LinuxHalNetwork::statusMonitorThread, this);

    wifi_status_ = checkWifiStatus();

    // Start network timeout (30 seconds)
    network_connected_ = false;
    startNetworkTimeout();

    ESP_LOGI(TAG, "Linux network initialized");
    return HalResult::OK;
}

HalResult LinuxHalNetwork::deinit()
{
    // Stop timeout thread
    stopNetworkTimeout();

    if (thread_running_.load())
    {
        thread_running_ = false;
        status_cv_.notify_one();

        if (status_thread_.joinable())
            status_thread_.join();
    }

    return HalResult::OK;
}

HalResult LinuxHalNetwork::scanWifi(std::vector<WifiConfig>& networks)
{
    networks.clear();

    // Use iwlist to scan for networks
    FILE* pipe = popen("iwlist scan 2>/dev/null | grep -E 'ESSID|Signal level'", "r");
    if (!pipe)
        return HalResult::ERROR;

    char buffer[256];
    WifiConfig current_network;
    bool has_ssid = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        std::string line(buffer);

        if (line.find("ESSID:") != std::string::npos)
        {
            size_t start = line.find("\"");
            size_t end = line.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos)
            {
                current_network.ssid = line.substr(start + 1, end - start - 1);
                has_ssid = true;
            }
        }
        else if (line.find("Signal level") != std::string::npos && has_ssid)
        {
            // Extract signal strength
            size_t pos = line.find("level=");
            if (pos != std::string::npos)
            {
                std::string level_str = line.substr(pos + 6);
                current_network.rssi = std::stoi(level_str);
            }

            if (!current_network.ssid.empty())
                networks.push_back(current_network);

            current_network = {};
            has_ssid = false;
        }
    }

    pclose(pipe);
    return HalResult::OK;
}

HalResult LinuxHalNetwork::connectWifi(const WifiConfig& config)
{
    // This is simplified - in a real implementation, you'd use NetworkManager or wpa_supplicant
    std::string command = "nmcli dev wifi connect \"" + config.ssid + "\" password \"" + config.password + "\"";
    int result = system(command.c_str());

    if (result == 0)
    {
        wifi_status_ = WifiStatus::CONNECTED;
        return HalResult::OK;
    }
    else
    {
        wifi_status_ = WifiStatus::ERROR;
        return HalResult::ERROR;
    }
}

HalResult LinuxHalNetwork::disconnectWifi()
{
    int result = system("nmcli dev disconnect iface wlan0");
    wifi_status_ = WifiStatus::DISCONNECTED;
    return (result == 0) ? HalResult::OK : HalResult::ERROR;
}

WifiStatus LinuxHalNetwork::getWifiStatus() const
{
    return wifi_status_;
}

HalResult LinuxHalNetwork::registerWifiCallback(WifiEventCallback callback)
{
    wifi_callback_ = callback;
    return HalResult::OK;
}

std::string LinuxHalNetwork::getLocalIp() const
{
    struct ifaddrs *ifaddrs_ptr, *ifa;
    char ip_str[INET_ADDRSTRLEN];
    std::string result;

    if (getifaddrs(&ifaddrs_ptr) == -1)
        return "";

    for (ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr) continue;

        // Check for IPv4 and not loopback
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);

            // Skip loopback
            if (std::string(ip_str) != "127.0.0.1")
            {
                result = std::string(ip_str);
                break;
            }
        }
    }

    freeifaddrs(ifaddrs_ptr);
    return result;
}

std::string LinuxHalNetwork::getMacAddress() const
{
    std::ifstream file("/sys/class/net/wlan0/address");
    if (file.is_open())
    {
        std::string mac;
        std::getline(file, mac);
        return mac;
    }

    // Try eth0 if wlan0 doesn't exist
    file.close();
    file.open("/sys/class/net/eth0/address");
    if (file.is_open())
    {
        std::string mac;
        std::getline(file, mac);
        return mac;
    }

    return "";
}

WifiStatus LinuxHalNetwork::checkWifiStatus()
{
#if 0
    FILE* pipe = popen("wpa_cli status", "r");
    if (!pipe)
        return WifiStatus::ERROR;

    char buffer[256];
    bool has_ssid = false;
    bool is_scanning = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        std::string line(buffer);

        if (line.find("wpa_state=SCANNING") != std::string::npos)
        {
            is_scanning = true;
            break;
        }
        else if (line.find("ssid=") != std::string::npos)
        {
            // Extract SSID to check if we have a connection
            size_t pos = line.find("ssid=");
            if (pos != std::string::npos)
            {
                std::string ssid = line.substr(pos + 5);
                // Remove newline and whitespace
                ssid.erase(ssid.find_last_not_of(" \n\r\t") + 1);
                if (!ssid.empty())
                    has_ssid = true;
            }
        }
    }

    pclose(pipe);

    if (is_scanning)
        return WifiStatus::DISCONNECTED;

    if (has_ssid)
        return WifiStatus::CONNECTED;
#endif

    return WifiStatus::DISCONNECTED;
}

void LinuxHalNetwork::statusMonitorThread() {
    WifiStatus last_status = wifi_status_;

    while (thread_running_)
    {
        WifiStatus current_status = checkWifiStatus();

        // Check if we have any network connection (WiFi or Ethernet)
        std::string localIp = getLocalIp();
        bool hasConnection = !localIp.empty();

        if (hasConnection && !network_connected_.load())
        {
            network_connected_ = true;
            stopNetworkTimeout();

            // Dispatch network events - assume Ethernet for Linux for now
            NetworkStatusChangedData statusData = { true, NetworkConnectionType::Ethernet };
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkStatusChanged, statusData));

            NetworkIpAssignedData ipData = {
                .ipAddress = localIp,
                .gateway = "192.168.1.1", // Simplified for Linux
                .netmask = "255.255.255.0",
                .connectionType = NetworkConnectionType::Ethernet,
                .ssid = "",
                .rssi = 0
            };
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkIpAssigned, ipData));

            // Simulate NTP sync like ESP32 does after network connection
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpSyncStarted));
            HAL::getInstance().getSystem().initNtp();
        }

        if (current_status != last_status)
        {
            wifi_status_ = current_status;
            if (wifi_callback_)
                wifi_callback_(wifi_status_);
            last_status = current_status;
        }

        // Wait for 5 seconds OR until thread_running_ becomes false
        std::unique_lock<std::mutex> lock(status_mutex_);
        status_cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !thread_running_.load(); });
    }
}

void LinuxHalNetwork::startNetworkTimeout()
{
    timeout_active_ = true;
    timeout_thread_ = std::thread(&LinuxHalNetwork::networkTimeoutTask, this);
}

void LinuxHalNetwork::stopNetworkTimeout()
{
    timeout_active_ = false;
    timeout_cv_.notify_one();

    if (timeout_thread_.joinable())
    {
        timeout_thread_.join();
    }
}

void LinuxHalNetwork::networkTimeoutTask()
{
    std::unique_lock<std::mutex> lock(timeout_mutex_);

    // Wait for 30 seconds OR until timeout_active_ becomes false
    if (timeout_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !timeout_active_.load(); }))
    {
        // Timeout was cancelled (timeout_active_ became false)
        return;
    }

    // Timeout expired
    if (timeout_active_.load() && !network_connected_.load())
    {
        ESP_LOGW(TAG, "Network connection timeout - no connection after 30 seconds");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkTimeout));
    }
}
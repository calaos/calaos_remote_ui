#include "linux_hal_network.h"
#include "logging.h"
#include "flux.h"
#include "hal.h"
#include "../calaos_config/device_config.h"
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
#include <dirent.h>
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

    // Log DeviceConfig state for debugging
    auto& devCfg = DeviceConfig::getInstance();
    ESP_LOGI(TAG, "DeviceConfig loaded=%s", devCfg.isLoaded() ? "true" : "false");
    if (devCfg.isLoaded())
    {
        ESP_LOGI(TAG, "DeviceConfig: interface=%s (%s)",
                 devCfg.getNetworkInterface().c_str(),
                 devCfg.isWifi() ? "wifi" : "ethernet");
        ESP_LOGI(TAG, "DeviceConfig: ip_mode=%s (%s)",
                 devCfg.getIpMode().c_str(),
                 devCfg.isStaticIp() ? "static" : "dhcp");
        if (devCfg.isWifi())
        {
            ESP_LOGI(TAG, "DeviceConfig: wifi_ssid='%s' wifi_password=%s",
                     devCfg.getWifiSsid().c_str(),
                     devCfg.getWifiPassword().empty() ? "(empty)" : "***");
        }
        if (devCfg.isStaticIp())
        {
            ESP_LOGI(TAG, "DeviceConfig: static_ip=%s mask=%s gw=%s dns=%s",
                     devCfg.getStaticIp().c_str(),
                     devCfg.getStaticMask().c_str(),
                     devCfg.getStaticGateway().c_str(),
                     devCfg.getStaticDns().c_str());
        }
        ESP_LOGI(TAG, "DeviceConfig: server_host=%s port=%u ssl=%s hasServerHost=%s",
                 devCfg.getServerHost().c_str(),
                 devCfg.getServerPort(),
                 devCfg.getServerSsl() ? "true" : "false",
                 devCfg.hasServerHost() ? "true" : "false");
    }
    else
    {
        ESP_LOGI(TAG, "No DeviceConfig loaded, using system defaults");
    }

    // If DeviceConfig says WiFi, try to auto-connect
    if (devCfg.isLoaded() && devCfg.isWifi())
        applyWifiConfig();

    // Start status monitoring thread
    thread_running_ = true;
    status_thread_ = std::thread(&LinuxHalNetwork::statusMonitorThread, this);

    wifi_status_ = checkWifiStatus();
    ESP_LOGI(TAG, "Initial wifi status: %d", static_cast<int>(wifi_status_));

    // Start network timeout (30 seconds)
    network_connected_ = false;
    startNetworkTimeout();

    ESP_LOGI(TAG, "Linux network initialized, waiting for connection...");
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
    std::string iface = findWirelessInterface();
    if (iface.empty())
    {
        ESP_LOGW(TAG, "No wireless interface found for disconnect");
        return HalResult::ERROR;
    }

    std::string command = "nmcli dev disconnect " + iface;
    int result = system(command.c_str());
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
    // Try to get MAC from the active non-loopback interface
    std::string iface = findActiveInterface();
    if (!iface.empty())
    {
        std::ifstream file("/sys/class/net/" + iface + "/address");
        if (file.is_open())
        {
            std::string mac;
            std::getline(file, mac);
            if (!mac.empty())
                return mac;
        }
    }

    // Fallback: iterate all interfaces in /sys/class/net/ and return the first valid MAC
    DIR *dir = opendir("/sys/class/net");
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name(entry->d_name);
            if (name == "." || name == ".." || name == "lo")
                continue;

            std::ifstream file("/sys/class/net/" + name + "/address");
            if (file.is_open())
            {
                std::string mac;
                std::getline(file, mac);
                if (!mac.empty() && mac != "00:00:00:00:00:00")
                {
                    closedir(dir);
                    return mac;
                }
            }
        }
        closedir(dir);
    }

    return "";
}

std::string LinuxHalNetwork::findActiveInterface() const
{
    struct ifaddrs *ifaddrs_ptr, *ifa;

    if (getifaddrs(&ifaddrs_ptr) == -1)
        return "";

    // Check if DeviceConfig constrains the interface type
    auto& devCfg = DeviceConfig::getInstance();
    bool wantWifi = devCfg.isLoaded() && devCfg.isWifi();
    bool wantEthernet = devCfg.isLoaded() && devCfg.isEthernet();

    std::string result;
    for (ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
            continue;

        // Look for IPv4, non-loopback, UP and RUNNING interface
        if (ifa->ifa_addr->sa_family == AF_INET && (ifa->ifa_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
        {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);

            if (std::string(ip_str) == "127.0.0.1")
                continue;

            std::string ifname(ifa->ifa_name);
            bool isWireless = isWirelessInterface(ifname);

            // Filter based on DeviceConfig preference
            if (wantWifi && !isWireless)
                continue;
            if (wantEthernet && isWireless)
                continue;

            result = ifname;
            break;
        }
    }

    freeifaddrs(ifaddrs_ptr);
    return result;
}

std::string LinuxHalNetwork::findWirelessInterface() const
{
    DIR *dir = opendir("/sys/class/net");
    if (!dir)
        return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name(entry->d_name);
        if (name == "." || name == "..")
            continue;

        // The presence of /sys/class/net/<iface>/wireless indicates a WiFi interface
        std::string wirelessPath = "/sys/class/net/" + name + "/wireless";
        DIR *wdir = opendir(wirelessPath.c_str());
        if (wdir)
        {
            closedir(wdir);
            closedir(dir);
            return name;
        }
    }

    closedir(dir);
    return "";
}

bool LinuxHalNetwork::isWirelessInterface(const std::string &ifname) const
{
    std::string wirelessPath = "/sys/class/net/" + ifname + "/wireless";
    DIR *dir = opendir(wirelessPath.c_str());
    if (dir)
    {
        closedir(dir);
        return true;
    }
    return false;
}

std::string LinuxHalNetwork::getDefaultGateway() const
{
    std::ifstream file("/proc/net/route");
    if (!file.is_open())
        return "";

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string iface, dest, gateway;
        iss >> iface >> dest >> gateway;

        // Default route has destination 00000000
        if (dest == "00000000")
        {
            // Convert hex gateway to dotted IP (little-endian on x86)
            unsigned long gw = std::stoul(gateway, nullptr, 16);
            struct in_addr addr;
            addr.s_addr = (in_addr_t)gw;
            return std::string(inet_ntoa(addr));
        }
    }

    return "";
}

std::string LinuxHalNetwork::getNetmaskForInterface(const std::string &ifname) const
{
    if (ifname.empty())
        return "";

    struct ifaddrs *ifaddrs_ptr, *ifa;
    if (getifaddrs(&ifaddrs_ptr) == -1)
        return "";

    std::string result;
    for (ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr || ifa->ifa_netmask == nullptr)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET && std::string(ifa->ifa_name) == ifname)
        {
            struct sockaddr_in *mask_in = (struct sockaddr_in *)ifa->ifa_netmask;
            char mask_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(mask_in->sin_addr), mask_str, INET_ADDRSTRLEN);
            result = std::string(mask_str);
            break;
        }
    }

    freeifaddrs(ifaddrs_ptr);
    return result;
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

            // Detect connection type and gather real network info
            std::string activeIface = findActiveInterface();
            bool isWifi = !activeIface.empty() && isWirelessInterface(activeIface);
            NetworkConnectionType connType = isWifi ? NetworkConnectionType::WiFi : NetworkConnectionType::Ethernet;

            ESP_LOGI(TAG, "Network connected: iface=%s type=%s ip=%s",
                     activeIface.c_str(),
                     isWifi ? "WiFi" : "Ethernet",
                     localIp.c_str());

            // Apply static IP if configured and not already applied
            auto& devCfg = DeviceConfig::getInstance();
            if (!staticIpApplied_ && devCfg.isLoaded() && devCfg.isStaticIp() && !activeIface.empty())
            {
                applyStaticIpConfig(activeIface);
                staticIpApplied_ = true;
                // Re-read the IP after applying static config
                localIp = getLocalIp();
                ESP_LOGI(TAG, "IP after static config: %s", localIp.c_str());
            }

            std::string gateway = getDefaultGateway();
            std::string netmask = getNetmaskForInterface(activeIface);
            std::string ssid;
            int rssi = 0;

            ESP_LOGI(TAG, "Network info: gateway=%s netmask=%s",
                     gateway.c_str(), netmask.c_str());

            if (isWifi)
            {
                // Try to get SSID via iwgetid
                FILE *pipe = popen("iwgetid -r 2>/dev/null", "r");
                if (pipe)
                {
                    char buf[128];
                    if (fgets(buf, sizeof(buf), pipe))
                    {
                        ssid = std::string(buf);
                        ssid.erase(ssid.find_last_not_of(" \n\r\t") + 1);
                    }
                    pclose(pipe);
                }
                ESP_LOGI(TAG, "WiFi connected: ssid=%s", ssid.c_str());
            }

            ESP_LOGI(TAG, "Dispatching NetworkStatusChanged (connected=%d) and NetworkIpAssigned (ip=%s)",
                     1, localIp.c_str());

            NetworkStatusChangedData statusData = { true, connType };
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkStatusChanged, statusData));

            NetworkIpAssignedData ipData = {
                .ipAddress = localIp,
                .gateway = gateway,
                .netmask = netmask,
                .connectionType = connType,
                .ssid = ssid,
                .rssi = rssi
            };
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkIpAssigned, ipData));

            // Simulate NTP sync like ESP32 does after network connection
            ESP_LOGI(TAG, "Dispatching NtpSyncStarted and initiating NTP");
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

void LinuxHalNetwork::applyWifiConfig()
{
    auto& devCfg = DeviceConfig::getInstance();
    if (!devCfg.isLoaded() || !devCfg.isWifi())
        return;

    std::string ssid = devCfg.getWifiSsid();
    std::string password = devCfg.getWifiPassword();

    if (ssid.empty())
    {
        ESP_LOGW(TAG, "WiFi SSID is empty, cannot auto-connect");
        return;
    }

    ESP_LOGI(TAG, "Auto-connecting WiFi to SSID '%s' from DeviceConfig", ssid.c_str());

    // Try nmcli first (NetworkManager)
    std::string nmcliCheck = "which nmcli >/dev/null 2>&1";
    if (system(nmcliCheck.c_str()) == 0)
    {
        ESP_LOGI(TAG, "Using nmcli for WiFi connection");
        std::string cmd = "nmcli dev wifi connect '" + ssid + "' password '" + password + "' 2>&1";
        FILE *pipe = popen(cmd.c_str(), "r");
        if (pipe)
        {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe))
                ESP_LOGI(TAG, "nmcli: %s", buf);
            int ret = pclose(pipe);
            if (ret == 0)
                ESP_LOGI(TAG, "nmcli WiFi connection successful");
            else
                ESP_LOGW(TAG, "nmcli WiFi connection failed (ret=%d)", ret);
        }
        return;
    }

    // Fallback: try wpa_supplicant via wpa_cli
    std::string wpaCheck = "which wpa_cli >/dev/null 2>&1";
    if (system(wpaCheck.c_str()) == 0)
    {
        ESP_LOGI(TAG, "Using wpa_cli for WiFi connection");

        std::string iface = findWirelessInterface();
        if (iface.empty())
        {
            ESP_LOGW(TAG, "No wireless interface found for wpa_cli");
            return;
        }

        // Add network using wpa_cli
        std::string addNet = "wpa_cli -i " + iface + " add_network 2>&1";
        FILE *pipe = popen(addNet.c_str(), "r");
        if (!pipe)
            return;

        char buf[64];
        std::string netId;
        if (fgets(buf, sizeof(buf), pipe))
        {
            netId = std::string(buf);
            netId.erase(netId.find_last_not_of(" \n\r\t") + 1);
        }
        pclose(pipe);

        if (netId.empty())
        {
            ESP_LOGW(TAG, "wpa_cli add_network failed");
            return;
        }

        // Set SSID
        std::string setSSID = "wpa_cli -i " + iface + " set_network " + netId + " ssid '\"" + ssid + "\"' 2>&1";
        system(setSSID.c_str());

        // Set password
        std::string setPSK = "wpa_cli -i " + iface + " set_network " + netId + " psk '\"" + password + "\"' 2>&1";
        system(setPSK.c_str());

        // Enable and select
        std::string enableNet = "wpa_cli -i " + iface + " enable_network " + netId + " 2>&1";
        system(enableNet.c_str());

        std::string selectNet = "wpa_cli -i " + iface + " select_network " + netId + " 2>&1";
        system(selectNet.c_str());

        ESP_LOGI(TAG, "wpa_cli WiFi connection initiated (network %s)", netId.c_str());
        return;
    }

    ESP_LOGW(TAG, "No WiFi management tool found (nmcli or wpa_cli)");
}

void LinuxHalNetwork::applyStaticIpConfig(const std::string &ifname)
{
    auto& devCfg = DeviceConfig::getInstance();
    if (!devCfg.isLoaded() || !devCfg.isStaticIp())
        return;

    ESP_LOGI(TAG, "Applying static IP on %s: ip=%s mask=%s gw=%s dns=%s",
             ifname.c_str(),
             devCfg.getStaticIp().c_str(),
             devCfg.getStaticMask().c_str(),
             devCfg.getStaticGateway().c_str(),
             devCfg.getStaticDns().c_str());

    // Convert netmask to CIDR prefix length
    struct in_addr maskAddr;
    int prefix = 24; // default
    if (inet_aton(devCfg.getStaticMask().c_str(), &maskAddr))
    {
        uint32_t mask = ntohl(maskAddr.s_addr);
        prefix = 0;
        while (mask & 0x80000000)
        {
            prefix++;
            mask <<= 1;
        }
    }

    // Flush existing addresses
    std::string flushCmd = "ip addr flush dev " + ifname + " 2>&1";
    ESP_LOGI(TAG, "  Running: %s", flushCmd.c_str());
    system(flushCmd.c_str());

    // Add static IP
    std::string addCmd = "ip addr add " + devCfg.getStaticIp() + "/" + std::to_string(prefix) + " dev " + ifname + " 2>&1";
    ESP_LOGI(TAG, "  Running: %s", addCmd.c_str());
    int ret = system(addCmd.c_str());
    if (ret != 0)
        ESP_LOGW(TAG, "  ip addr add failed (ret=%d)", ret);

    // Add default route
    if (!devCfg.getStaticGateway().empty())
    {
        std::string routeCmd = "ip route add default via " + devCfg.getStaticGateway() + " dev " + ifname + " 2>&1";
        ESP_LOGI(TAG, "  Running: %s", routeCmd.c_str());
        ret = system(routeCmd.c_str());
        if (ret != 0)
            ESP_LOGW(TAG, "  ip route add failed (ret=%d)", ret);
    }

    // Write DNS
    if (!devCfg.getStaticDns().empty())
    {
        std::ofstream resolvConf("/etc/resolv.conf");
        if (resolvConf.is_open())
        {
            resolvConf << "nameserver " << devCfg.getStaticDns() << std::endl;
            resolvConf.close();
            ESP_LOGI(TAG, "  DNS written to /etc/resolv.conf: %s", devCfg.getStaticDns().c_str());
        }
        else
        {
            ESP_LOGW(TAG, "  Failed to write /etc/resolv.conf");
        }
    }

    ESP_LOGI(TAG, "Static IP configuration applied on %s", ifname.c_str());
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
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(timeout_mutex_);

            // Wait 30 seconds for connection OR until cancelled
            if (timeout_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !timeout_active_.load(); }))
                return; // Cancelled (deinit or connection succeeded)
        }

        // Check if network connected during the wait
        if (network_connected_.load())
            return;

        if (!timeout_active_.load())
            return;

        ESP_LOGW(TAG, "Network connection timeout - no connection after 30 seconds");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkTimeout));

        // Wait 30 seconds before retrying (cancellable)
        {
            std::unique_lock<std::mutex> lock(timeout_mutex_);
            ESP_LOGI(TAG, "Waiting 30s before network retry...");
            if (timeout_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !timeout_active_.load(); }))
                return; // Cancelled
        }

        // Check again if network connected during the retry wait
        if (network_connected_.load())
        {
            ESP_LOGI(TAG, "Network connected during retry wait, aborting retry");
            return;
        }

        if (!timeout_active_.load())
            return;

        ESP_LOGI(TAG, "Retrying network connection");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkRetryStarted));
        retryConnection();
    }
}

void LinuxHalNetwork::retryConnection()
{
    network_connected_ = false;
    staticIpApplied_ = false;

    auto& devCfg = DeviceConfig::getInstance();
    if (devCfg.isLoaded() && devCfg.isWifi())
        applyWifiConfig();

    // The statusMonitorThread polls every 5s and will detect connection automatically.
    // The timeout loop in networkTimeoutTask will continue waiting for the next 30s.
}
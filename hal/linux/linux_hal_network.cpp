#include "linux_hal_network.h"
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

HalResult LinuxHalNetwork::init() {
    try {
        std::cout << "Initializing Linux network" << std::endl;
        
        // Start status monitoring thread
        thread_running_ = true;
        status_thread_ = std::thread(&LinuxHalNetwork::statusMonitorThread, this);
        
        wifi_status_ = checkWifiStatus();
        
        std::cout << "Linux network initialized" << std::endl;
        return HalResult::OK;
    } catch (...) {
        std::cerr << "Exception during Linux network init" << std::endl;
        return HalResult::ERROR;
    }
}

HalResult LinuxHalNetwork::deinit() {
    thread_running_ = false;
    
    if (status_thread_.joinable()) {
        status_thread_.join();
    }
    
    return HalResult::OK;
}

HalResult LinuxHalNetwork::scanWifi(std::vector<WifiConfig>& networks) {
    networks.clear();
    
    // Use iwlist to scan for networks
    FILE* pipe = popen("iwlist scan 2>/dev/null | grep -E 'ESSID|Signal level'", "r");
    if (!pipe) {
        return HalResult::ERROR;
    }
    
    char buffer[256];
    WifiConfig current_network;
    bool has_ssid = false;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        if (line.find("ESSID:") != std::string::npos) {
            size_t start = line.find("\"");
            size_t end = line.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                current_network.ssid = line.substr(start + 1, end - start - 1);
                has_ssid = true;
            }
        } else if (line.find("Signal level") != std::string::npos && has_ssid) {
            // Extract signal strength
            size_t pos = line.find("level=");
            if (pos != std::string::npos) {
                std::string level_str = line.substr(pos + 6);
                current_network.rssi = std::stoi(level_str);
            }
            
            if (!current_network.ssid.empty()) {
                networks.push_back(current_network);
            }
            
            current_network = {};
            has_ssid = false;
        }
    }
    
    pclose(pipe);
    return HalResult::OK;
}

HalResult LinuxHalNetwork::connectWifi(const WifiConfig& config) {
    // This is simplified - in a real implementation, you'd use NetworkManager or wpa_supplicant
    std::string command = "nmcli dev wifi connect \"" + config.ssid + "\" password \"" + config.password + "\"";
    int result = system(command.c_str());
    
    if (result == 0) {
        wifi_status_ = WifiStatus::CONNECTED;
        return HalResult::OK;
    } else {
        wifi_status_ = WifiStatus::ERROR;
        return HalResult::ERROR;
    }
}

HalResult LinuxHalNetwork::disconnectWifi() {
    int result = system("nmcli dev disconnect iface wlan0");
    wifi_status_ = WifiStatus::DISCONNECTED;
    return (result == 0) ? HalResult::OK : HalResult::ERROR;
}

WifiStatus LinuxHalNetwork::getWifiStatus() const {
    return wifi_status_;
}

HalResult LinuxHalNetwork::registerWifiCallback(WifiEventCallback callback) {
    wifi_callback_ = callback;
    return HalResult::OK;
}

std::string LinuxHalNetwork::getLocalIP() const {
    struct ifaddrs *ifaddrs_ptr, *ifa;
    char ip_str[INET_ADDRSTRLEN];
    std::string result;
    
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        return "";
    }
    
    for (ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // Check for IPv4 and not loopback
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);
            
            // Skip loopback
            if (std::string(ip_str) != "127.0.0.1") {
                result = std::string(ip_str);
                break;
            }
        }
    }
    
    freeifaddrs(ifaddrs_ptr);
    return result;
}

std::string LinuxHalNetwork::getMacAddress() const {
    std::ifstream file("/sys/class/net/wlan0/address");
    if (file.is_open()) {
        std::string mac;
        std::getline(file, mac);
        return mac;
    }
    
    // Try eth0 if wlan0 doesn't exist
    file.close();
    file.open("/sys/class/net/eth0/address");
    if (file.is_open()) {
        std::string mac;
        std::getline(file, mac);
        return mac;
    }
    
    return "";
}

WifiStatus LinuxHalNetwork::checkWifiStatus() {
    // Check network connectivity
    FILE* pipe = popen("nmcli -t -f WIFI g", "r");
    if (!pipe) return WifiStatus::ERROR;
    
    char buffer[64];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string status(buffer);
        if (status.find("enabled") != std::string::npos) {
            pclose(pipe);
            
            // Check if connected
            pipe = popen("nmcli -t -f STATE g", "r");
            if (pipe && fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string state(buffer);
                pclose(pipe);
                
                if (state.find("connected") != std::string::npos) {
                    return WifiStatus::CONNECTED;
                } else {
                    return WifiStatus::DISCONNECTED;
                }
            }
        }
    }
    
    if (pipe) pclose(pipe);
    return WifiStatus::DISCONNECTED;
}

void LinuxHalNetwork::statusMonitorThread() {
    WifiStatus last_status = wifi_status_;
    
    while (thread_running_) {
        WifiStatus current_status = checkWifiStatus();
        
        if (current_status != last_status) {
            wifi_status_ = current_status;
            if (wifi_callback_) {
                wifi_callback_(wifi_status_);
            }
            last_status = current_status;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
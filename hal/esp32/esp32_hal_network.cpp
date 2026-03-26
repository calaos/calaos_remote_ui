#include "esp32_hal_network.h"
#include "esp32_hal_system.h"
#include "logging.h"
#include "esp_netif.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>
#include "ethernet_init.h"
#include "esp_eth.h"
#include "esp_hosted.h"
#include "flux.h"
#include "../hal.h"
#include "../calaos_config/device_config.h"

static const char* TAG = "hal.network";

// Static members initialization
QueueHandle_t Esp32HalNetwork::timeoutQueue = nullptr;
TaskHandle_t Esp32HalNetwork::timeoutTaskHandle = nullptr;
SemaphoreHandle_t Esp32HalNetwork::retrySemaphore_ = nullptr;
Esp32HalNetwork* Esp32HalNetwork::retryInstance_ = nullptr;

// Forward declaration for NTP sync task
static void ntpSyncTask(void* arg);

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "MASK: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

    char ipStr[16], gwStr[16], netmaskStr[16];
    snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ip_info->ip));
    snprintf(gwStr, sizeof(gwStr), IPSTR, IP2STR(&ip_info->gw));
    snprintf(netmaskStr, sizeof(netmaskStr), IPSTR, IP2STR(&ip_info->netmask));

    // Dispatch network connected event
    NetworkStatusChangedData statusData = { true, NetworkConnectionType::Ethernet };
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkStatusChanged, statusData));

    // Dispatch IP assigned event
    NetworkIpAssignedData ipData = {
        .ipAddress = std::string(ipStr),
        .gateway = std::string(gwStr),
        .netmask = std::string(netmaskStr),
        .connectionType = NetworkConnectionType::Ethernet,
        .ssid = "",
        .rssi = 0
    };
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkIpAssigned, ipData));

    // Stop network timeout - we have a connection
    Esp32HalNetwork* networkHal = static_cast<Esp32HalNetwork*>(arg);
    if (networkHal)
        networkHal->onNetworkConnected();

    // Start NTP sync in a separate task (blocking operation)
    if (!HAL::getInstance().getSystem().isTimeSynced())
        xTaskCreate(ntpSyncTask, "ntp_sync", 4096, nullptr, 5, nullptr);
}

// NTP synchronization task - runs blocking NTP sync after network connection
static void ntpSyncTask(void* arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting NTP time synchronization");

    // Dispatch NtpSyncStarted event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpSyncStarted));

    // Initialize and wait for NTP sync
    HalSystem& system = HAL::getInstance().getSystem();

    HalResult initResult = system.initNtp();
    if (initResult != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NTP");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpSyncFailed));
        system.startNtpRetryTimer();
        vTaskDelete(nullptr);
        return;
    }

    // Wait for sync with 15 second timeout
    HalResult syncResult = system.waitForTimeSync(15000);
    if (syncResult == HalResult::OK)
    {
        ESP_LOGI(TAG, "NTP time synchronization successful");
        // NtpTimeSynced event is dispatched by onNtpSyncComplete() callback
    }
    else
    {
        ESP_LOGW(TAG, "NTP time synchronization failed, starting retry timer");
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NtpSyncFailed));
        system.startNtpRetryTimer();
    }

    vTaskDelete(nullptr);
}

HalResult Esp32HalNetwork::init()
{
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    // Read DeviceConfig to determine which network interface to use
    auto& devCfg = DeviceConfig::getInstance();
    ESP_LOGI(TAG, "DeviceConfig: loaded=%s", devCfg.isLoaded() ? "true" : "false");
    if (devCfg.isLoaded())
    {
        ESP_LOGI(TAG, "DeviceConfig: interface=%s ip_mode=%s",
                 devCfg.getNetworkInterface().c_str(), devCfg.getIpMode().c_str());
        ESP_LOGI(TAG, "DeviceConfig: server_host='%s' port=%u ssl=%s hasServerHost=%s",
                 devCfg.getServerHost().c_str(), devCfg.getServerPort(),
                 devCfg.getServerSsl() ? "true" : "false",
                 devCfg.hasServerHost() ? "true" : "false");
    }

    // Decide which interface to initialize based on DeviceConfig
    bool useWifi = devCfg.isLoaded() && devCfg.isWifi();
    bool useEthernet = !useWifi; // Default: ethernet if no config or config=ethernet

    ESP_LOGI(TAG, "Network mode: %s", useWifi ? "WiFi" : "Ethernet");

    if (useEthernet)
    {
        HalResult ethResult = initEthernet();
        if (ethResult != HalResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize ethernet");
            return ethResult;
        }
    }
    else
    {
        HalResult wifiResult = initWifi();
        if (wifiResult != HalResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize WiFi");
            return wifiResult;
        }
    }

    // Create timeout queue, semaphore and task (only once, static)
    if (timeoutQueue == nullptr)
    {
        timeoutQueue = xQueueCreate(5, sizeof(uint32_t));
        retrySemaphore_ = xSemaphoreCreateBinary();
        retryInstance_ = this;
        xTaskCreate(
            networkTimeoutTask,
            "network_timeout",
            4096,
            nullptr,
            5,
            &timeoutTaskHandle
        );
    }

    // Create and start network timeout timer (30 seconds)
    networkTimeoutTimer = xTimerCreate(
        "network_timeout",
        pdMS_TO_TICKS(30000),
        pdFALSE,
        this,
        networkTimeoutCallback
    );

    networkConnected = false;
    startNetworkTimeout();

    return HalResult::OK;
}

HalResult Esp32HalNetwork::initEthernet()
{
    ESP_LOGI(TAG, "Initializing Ethernet");

    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(ethernet_init_all(&eth_handles, &eth_port_cnt));

    if (eth_port_cnt == 1)
    {
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
        ethHandle = eth_handles[0];
    }
    else if (eth_port_cnt > 1)
    {
        ESP_LOGW(TAG, "Multiple eth ports (%d). Only using first one.", eth_port_cnt);
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
        ethHandle = eth_handles[0];
    }
    else
    {
        ESP_LOGE(TAG, "No ethernet ports found");
        return HalResult::ERROR;
    }

    // Register IP event handler for ethernet
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, this));

    // Register ETH event handler for link-up (used for static IP application)
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &ethEventHandler, this));

    // Start Ethernet driver
    for (int i = 0; i < eth_port_cnt; i++)
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));

    // Print device info
    for (int i = 0; i < eth_port_cnt; i++)
    {
        eth_dev_info_t info = ethernet_init_get_dev_info(&eth_handles[i]);
        if (info.type == ETH_DEV_TYPE_INTERNAL_ETH)
        {
            ESP_LOGI(TAG, "Device Name: %s", info.name);
            ESP_LOGI(TAG, "Device type: ETH_DEV_TYPE_INTERNAL_ETH(%d)", info.type);
            ESP_LOGI(TAG, "Pins: mdc: %d, mdio: %d", info.pin.eth_internal_mdc, info.pin.eth_internal_mdio);
        }
        else if (info.type == ETH_DEV_TYPE_SPI)
        {
            ESP_LOGI(TAG, "Device Name: %s", info.name);
            ESP_LOGI(TAG, "Device type: ETH_DEV_TYPE_SPI(%d)", info.type);
            ESP_LOGI(TAG, "Pins: cs: %d, intr: %d", info.pin.eth_spi_cs, info.pin.eth_spi_int);
        }
    }

    ethInitialized_ = true;
    ESP_LOGI(TAG, "Ethernet initialized successfully");
    return HalResult::OK;
}

HalResult Esp32HalNetwork::initWifi()
{
    ESP_LOGI(TAG, "Initializing WiFi");

    esp_netif_create_default_wifi_sta();

    esp_hosted_coprocessor_fwver_t ver_info = {};
    esp_err_t ret = esp_hosted_get_coprocessor_fwversion(&ver_info);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "Failed to get coprocessor fw version: %s", esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "Hosted Coprocessor FW Version: %d.%d.%d", ver_info.major1, ver_info.minor1, ver_info.patch1);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init wifi: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             ESP_EVENT_ANY_ID,
                                             &wifiEventHandler,
                                             this,
                                             &wifiHandlerInstance);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register wifi handler: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                             IP_EVENT_STA_GOT_IP,
                                             &wifiEventHandler,
                                             this,
                                             &ipHandlerInstance);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register IP handler: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set wifi mode: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start wifi: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    // If DeviceConfig has WiFi credentials, set them and connect
    auto& devCfg = DeviceConfig::getInstance();
    if (devCfg.isLoaded() && devCfg.isWifi())
    {
        ESP_LOGI(TAG, "Connecting to WiFi SSID '%s' from DeviceConfig", devCfg.getWifiSsid().c_str());

        wifi_config_t provWifiCfg = {};
        strncpy((char*)provWifiCfg.sta.ssid, devCfg.getWifiSsid().c_str(), sizeof(provWifiCfg.sta.ssid) - 1);
        strncpy((char*)provWifiCfg.sta.password, devCfg.getWifiPassword().c_str(), sizeof(provWifiCfg.sta.password) - 1);
        provWifiCfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        provWifiCfg.sta.pmf_cfg.capable = true;
        provWifiCfg.sta.pmf_cfg.required = false;

        ret = esp_wifi_set_config(WIFI_IF_STA, &provWifiCfg);
        if (ret != ESP_OK)
            ESP_LOGW(TAG, "Failed to set wifi config from DeviceConfig: %s", esp_err_to_name(ret));
        else
        {
            ret = esp_wifi_connect();
            if (ret != ESP_OK)
                ESP_LOGW(TAG, "Failed to connect wifi: %s", esp_err_to_name(ret));
            else
                ESP_LOGI(TAG, "WiFi connect initiated from DeviceConfig");
        }
    }

    wifiInitialized_ = true;
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return HalResult::OK;
}

HalResult Esp32HalNetwork::deinit()
{
    // Stop and delete timeout timer
    if (networkTimeoutTimer)
    {
        xTimerStop(networkTimeoutTimer, 0);
        xTimerDelete(networkTimeoutTimer, 0);
        networkTimeoutTimer = nullptr;
    }

    if (wifiInitialized_)
    {
        esp_wifi_stop();
        esp_wifi_deinit();
        wifiInitialized_ = false;
        ESP_LOGI(TAG, "WiFi deinitialized");
    }

    if (ethInitialized_)
    {
        if (ethHandle)
        {
            esp_eth_stop(ethHandle);
            ethHandle = nullptr;
        }
        ethInitialized_ = false;
        ESP_LOGI(TAG, "Ethernet deinitialized");
    }

    return HalResult::OK;
}

HalResult Esp32HalNetwork::scanWifi(std::vector<WifiConfig>& networks)
{
    networks.clear();

    wifi_scan_config_t scanConfig = {};
    scanConfig.ssid = nullptr;
    scanConfig.bssid = nullptr;
    scanConfig.channel = 0;
    scanConfig.show_hidden = false;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 100;
    scanConfig.scan_time.active.max = 300;

    esp_err_t ret = esp_wifi_scan_start(&scanConfig, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    uint16_t apCount = 0;
    ret = esp_wifi_scan_get_ap_num(&apCount);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    if (apCount > 0)
    {
        wifi_ap_record_t* apRecords = new wifi_ap_record_t[apCount];
        ret = esp_wifi_scan_get_ap_records(&apCount, apRecords);
        if (ret == ESP_OK)
        {
            for (int i = 0; i < apCount; i++)
            {
                WifiConfig config;
                config.ssid = std::string((char*)apRecords[i].ssid);
                config.rssi = apRecords[i].rssi;
                memcpy(config.bssid, apRecords[i].bssid, 6);
                networks.push_back(config);
            }
        }
        delete[] apRecords;
    }

    return HalResult::OK;
}

HalResult Esp32HalNetwork::connectWifi(const WifiConfig& config)
{
    wifi_config_t wifiConfig = {};
    strncpy((char*)wifiConfig.sta.ssid, config.ssid.c_str(), sizeof(wifiConfig.sta.ssid) - 1);
    strncpy((char*)wifiConfig.sta.password, config.password.c_str(), sizeof(wifiConfig.sta.password) - 1);
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifiConfig.sta.pmf_cfg.capable = true;
    wifiConfig.sta.pmf_cfg.required = false;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set wifi config: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect wifi: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    wifiStatus = WifiStatus::CONNECTING;
    return HalResult::OK;
}

HalResult Esp32HalNetwork::disconnectWifi()
{
    esp_err_t ret = esp_wifi_disconnect();
    wifiStatus = WifiStatus::DISCONNECTED;
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

WifiStatus Esp32HalNetwork::getWifiStatus() const
{
    return wifiStatus;
}

HalResult Esp32HalNetwork::registerWifiCallback(WifiEventCallback callback)
{
    wifiCallback = callback;
    return HalResult::OK;
}

std::string Esp32HalNetwork::getLocalIp() const
{
    // Check the interface that was actually initialized
    const char* ifkey = ethInitialized_ ? "ETH_DEF" : "WIFI_STA_DEF";
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (!netif)
        return "";

    esp_netif_ip_info_t ipInfo;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ipInfo);
    if (ret != ESP_OK)
        return "";

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ipInfo.ip));
    return std::string(ipStr);
}

std::string Esp32HalNetwork::getMacAddress() const
{
    uint8_t mac[6];

    // Try Ethernet first if connected
    if (ethernetConnected && ethHandle)
    {
        esp_err_t ret = esp_eth_ioctl(ethHandle, ETH_CMD_G_MAC_ADDR, mac);
        if (ret == ESP_OK)
        {
            auto macStr = std::make_unique<char[]>(18);
            snprintf(macStr.get(), 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return std::string(macStr.get());
        }
    }

    // Fallback to WiFi if connected
    if (wifiConnected)
    {
        esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (ret == ESP_OK)
        {
            auto macStr = std::make_unique<char[]>(18);
            snprintf(macStr.get(), 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return std::string(macStr.get());
        }
    }

    return "";
}

void Esp32HalNetwork::applyStaticIpConfig(esp_netif_t *netif, const char *ifkey)
{
    auto& devCfg = DeviceConfig::getInstance();
    if (!devCfg.isLoaded() || !devCfg.isStaticIp())
        return;

    ESP_LOGI(TAG, "Applying static IP on %s: ip=%s mask=%s gw=%s dns=%s",
             ifkey,
             devCfg.getStaticIp().c_str(),
             devCfg.getStaticMask().c_str(),
             devCfg.getStaticGateway().c_str(),
             devCfg.getStaticDns().c_str());

    // Stop DHCP client first
    esp_err_t ret = esp_netif_dhcpc_stop(netif);
    ESP_LOGI(TAG, "  DHCP client stop: %s", esp_err_to_name(ret));

    esp_netif_ip_info_t ipInfo = {};
    esp_netif_str_to_ip4(devCfg.getStaticIp().c_str(), &ipInfo.ip);
    esp_netif_str_to_ip4(devCfg.getStaticMask().c_str(), &ipInfo.netmask);
    esp_netif_str_to_ip4(devCfg.getStaticGateway().c_str(), &ipInfo.gw);

    ret = esp_netif_set_ip_info(netif, &ipInfo);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "  Failed to set static IP: %s", esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "  Static IP set successfully (will trigger IP event)");

    // Set DNS if provided
    if (!devCfg.getStaticDns().empty())
    {
        esp_netif_dns_info_t dnsInfo = {};
        esp_netif_str_to_ip4(devCfg.getStaticDns().c_str(), &dnsInfo.ip.u_addr.ip4);
        dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
        ret = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dnsInfo);
        ESP_LOGI(TAG, "  DNS set: %s (result: %s)",
                 devCfg.getStaticDns().c_str(), esp_err_to_name(ret));
    }
}

void Esp32HalNetwork::ethEventHandler(void* arg, esp_event_base_t eventBase,
                                      int32_t eventId, void* eventData)
{
    Esp32HalNetwork* self = static_cast<Esp32HalNetwork*>(arg);

    if (eventId == ETHERNET_EVENT_CONNECTED)
    {
        ESP_LOGI(TAG, "Ethernet link up");
        self->ethernetConnected = true;

        // Apply static IP right after link-up (before DHCP kicks in)
        auto& devCfg = DeviceConfig::getInstance();
        if (devCfg.isLoaded() && devCfg.isStaticIp())
        {
            esp_netif_t* ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (ethNetif)
                applyStaticIpConfig(ethNetif, "ETH_DEF");
            else
                ESP_LOGW(TAG, "ETH_DEF netif not found for static IP");
        }
    }
    else if (eventId == ETHERNET_EVENT_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Ethernet link down");
        self->ethernetConnected = false;

        NetworkStatusChangedData statusData = { false, NetworkConnectionType::Ethernet };
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkStatusChanged, statusData));
    }
    else if (eventId == ETHERNET_EVENT_START)
    {
        ESP_LOGI(TAG, "Ethernet started");
    }
    else if (eventId == ETHERNET_EVENT_STOP)
    {
        ESP_LOGI(TAG, "Ethernet stopped");
    }
}

void Esp32HalNetwork::wifiEventHandler(void* arg, esp_event_base_t eventBase,
                                       int32_t eventId, void* eventData)
{
    Esp32HalNetwork* self = static_cast<Esp32HalNetwork*>(arg);

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi started");
    }
    else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(TAG, "WiFi connected to AP");

        // Apply static IP right after association (before DHCP starts)
        auto& devCfg = DeviceConfig::getInstance();
        if (devCfg.isLoaded() && devCfg.isStaticIp())
        {
            esp_netif_t* staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (staNetif)
                applyStaticIpConfig(staNetif, "WIFI_STA_DEF");
            else
                ESP_LOGW(TAG, "WIFI_STA_DEF netif not found for static IP");
        }
    }
    else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WiFi disconnected");
        self->wifiStatus = WifiStatus::DISCONNECTED;
        self->wifiConnected = false;
        if (self->wifiCallback)
            self->wifiCallback(self->wifiStatus);
    }
    else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        self->wifiStatus = WifiStatus::CONNECTED;
        self->wifiConnected = true;

        // Get WiFi info (SSID, RSSI)
        wifi_ap_record_t apInfo;
        std::string ssid = "";
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK)
        {
            ssid = std::string((char*)apInfo.ssid);
            rssi = apInfo.rssi;
        }

        // Convert IP info to strings
        char ipStr[16], gwStr[16], netmaskStr[16];
        snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(gwStr, sizeof(gwStr), IPSTR, IP2STR(&event->ip_info.gw));
        snprintf(netmaskStr, sizeof(netmaskStr), IPSTR, IP2STR(&event->ip_info.netmask));

        // Dispatch network connected event
        NetworkStatusChangedData statusData = { true, NetworkConnectionType::WiFi };
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkStatusChanged, statusData));

        // Dispatch IP assigned event with WiFi details
        NetworkIpAssignedData ipData = {
            .ipAddress = std::string(ipStr),
            .gateway = std::string(gwStr),
            .netmask = std::string(netmaskStr),
            .connectionType = NetworkConnectionType::WiFi,
            .ssid = ssid,
            .rssi = rssi
        };
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkIpAssigned, ipData));

        // Stop network timeout - WiFi connected
        self->networkConnected = true;
        self->stopNetworkTimeout();

        if (self->wifiCallback)
            self->wifiCallback(self->wifiStatus);

        // Start NTP sync in a separate task (blocking operation)
        // Only if not already synced (ethernet might have already synced)
        if (!HAL::getInstance().getSystem().isTimeSynced())
        {
            xTaskCreate(ntpSyncTask, "ntp_sync", 4096, nullptr, 5, nullptr);
        }
    }
}

void Esp32HalNetwork::onNetworkConnected()
{
    networkConnected = true;
    ethernetConnected = true;
    stopNetworkTimeout();
}

void Esp32HalNetwork::startNetworkTimeout()
{
    if (networkTimeoutTimer)
        xTimerStart(networkTimeoutTimer, 0);
}

void Esp32HalNetwork::stopNetworkTimeout()
{
    if (networkTimeoutTimer)
        xTimerStop(networkTimeoutTimer, 0);

    // Wake up any pending retry sleep so it can check networkConnected and abort
    if (retrySemaphore_)
        xSemaphoreGive(retrySemaphore_);
}

void Esp32HalNetwork::networkTimeoutCallback(TimerHandle_t timer)
{
    Esp32HalNetwork* networkHal = static_cast<Esp32HalNetwork*>(pvTimerGetTimerID(timer));
    if (networkHal && !networkHal->networkConnected)
    {
        ESP_LOGW(TAG, "Network connection timeout - no connection after 30 seconds");

        // Send timeout signal to the task via queue (safe from timer context)
        uint32_t timeoutSignal = 1;
        if (timeoutQueue)
        {
            xQueueSend(timeoutQueue, &timeoutSignal, 0); // Don't block in timer
        }
    }
}

void Esp32HalNetwork::networkTimeoutTask(void* parameter)
{
    uint32_t signal;

    while (true)
    {
        // Wait for timeout signals from timer callback
        if (xQueueReceive(timeoutQueue, &signal, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGW(TAG, "Processing network timeout in task context");
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkTimeout));

            // Wait 30 seconds before retrying (cancellable via retrySemaphore_)
            ESP_LOGI(TAG, "Waiting 30s before network retry...");
            xSemaphoreTake(retrySemaphore_, pdMS_TO_TICKS(30000));

            // Check if network connected during the wait
            if (retryInstance_ && retryInstance_->networkConnected)
            {
                ESP_LOGI(TAG, "Network connected during retry wait, aborting retry");
                continue;
            }

            ESP_LOGI(TAG, "Retrying network connection");
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkRetryStarted));

            if (retryInstance_)
                retryInstance_->retryConnection();
        }
    }
}

void Esp32HalNetwork::retryConnection()
{
    networkConnected = false;

    if (wifiInitialized_)
    {
        ESP_LOGI(TAG, "Retrying WiFi connection");
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK)
            ESP_LOGW(TAG, "WiFi connect retry failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Retrying Ethernet connection (waiting for link)");
    }

    startNetworkTimeout();
}
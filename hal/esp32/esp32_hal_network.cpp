#include "esp32_hal_network.h"
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

static const char* TAG = "hal.network";

// Static members initialization
QueueHandle_t Esp32HalNetwork::timeoutQueue = nullptr;
TaskHandle_t Esp32HalNetwork::timeoutTaskHandle = nullptr;

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

    // Convert IP info to strings
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
        .ssid = "",  // Empty for Ethernet
        .rssi = 0    // Not applicable for Ethernet
    };
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkIpAssigned, ipData));
    
    // Stop network timeout - we have a connection
    Esp32HalNetwork* networkHal = static_cast<Esp32HalNetwork*>(arg);
    if (networkHal)
    {
        networkHal->onNetworkConnected();
    }
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

    //Init ethernet
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(ethernet_init_all(&eth_handles, &eth_port_cnt));

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1)
    {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    }
    else
    {
        ESP_LOGW(TAG, "multiple eth port. not implemented");
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, this));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++)
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));

    // Print each device info
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

    //Init wifi (optional - ethernet is sufficient for network connectivity)
    ESP_LOGI(TAG, "Initializing WiFi (optional)");
    esp_netif_create_default_wifi_sta();

    esp_hosted_coprocessor_fwver_t ver_info = {};
    ret = esp_hosted_get_coprocessor_fwversion(&ver_info);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to get coprocessor fw version: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Hosted Coprocessor FW Version: %d.%d.%d", ver_info.major1, ver_info.minor1, ver_info.patch1);
    }

    // Use simplified WiFi configuration
    // wifi_init_config_t cfg = {};
    // cfg.osi_funcs = &g_wifi_osi_funcs;
    // cfg.wpa_crypto_funcs = g_wifi_default_wpa_crypto_funcs;
    // cfg.static_rx_buf_num = 10;
    // cfg.dynamic_rx_buf_num = 32;
    // cfg.tx_buf_type = 1;
    // cfg.dynamic_tx_buf_num = 16;
    // cfg.ampdu_rx_enable = 1;
    // cfg.ampdu_tx_enable = 1;
    // cfg.nvs_enable = 1;
    // cfg.magic = WIFI_INIT_CONFIG_MAGIC;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to init wifi: %s (continuing with ethernet only)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Network initialized successfully (ethernet only)");
        return HalResult::OK;  // Ethernet is sufficient
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             ESP_EVENT_ANY_ID,
                                             &wifiEventHandler,
                                             this,
                                             &wifiHandlerInstance);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to register wifi handler: %s (continuing with ethernet only)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Network initialized successfully (ethernet only)");
        return HalResult::OK;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                             IP_EVENT_STA_GOT_IP,
                                             &wifiEventHandler,
                                             this,
                                             &ipHandlerInstance);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to register IP handler: %s (continuing with ethernet only)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Network initialized successfully (ethernet only)");
        return HalResult::OK;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to set wifi mode: %s (continuing with ethernet only)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Network initialized successfully (ethernet only)");
        return HalResult::OK;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to start wifi: %s (continuing with ethernet only)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Network initialized successfully (ethernet only)");
        return HalResult::OK;
    }

    ESP_LOGI(TAG, "WiFi initialized successfully");
    ESP_LOGI(TAG, "Network initialized successfully (ethernet + wifi)");
    
    // Create timeout queue and task (only once, static)
    if (timeoutQueue == nullptr)
    {
        timeoutQueue = xQueueCreate(5, sizeof(uint32_t));
        
        xTaskCreate(
            networkTimeoutTask,
            "network_timeout",
            4096,  // Larger stack size for safety
            nullptr,
            5,     // Priority
            &timeoutTaskHandle
        );
    }
    
    // Create and start network timeout timer (30 seconds)
    networkTimeoutTimer = xTimerCreate(
        "network_timeout",
        pdMS_TO_TICKS(30000), // 30 seconds
        pdFALSE,              // Not periodic
        this,                 // Timer ID (pass this pointer)
        networkTimeoutCallback
    );
    
    networkConnected = false;
    startNetworkTimeout();
    
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
    
    esp_wifi_stop();
    esp_wifi_deinit();
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
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
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
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret != ESP_OK)
        return "";

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(macStr);
}

void Esp32HalNetwork::wifiEventHandler(void* arg, esp_event_base_t eventBase,
                                       int32_t eventId, void* eventData)
{
    Esp32HalNetwork* self = static_cast<Esp32HalNetwork*>(arg);

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi started");
    }
    else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WiFi disconnected");
        self->wifiStatus = WifiStatus::DISCONNECTED;
        if (self->wifiCallback)
            self->wifiCallback(self->wifiStatus);
    }
    else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        self->wifiStatus = WifiStatus::CONNECTED;

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
        self->onNetworkConnected();

        if (self->wifiCallback)
            self->wifiCallback(self->wifiStatus);
    }
}

void Esp32HalNetwork::onNetworkConnected()
{
    networkConnected = true;
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
            // Now we can safely dispatch the event from task context
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::NetworkTimeout));
        }
    }
}
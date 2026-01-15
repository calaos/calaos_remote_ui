#include "calaos_discovery.h"
#include "logging.h"
#include <cstring>

static const char* TAG = "calaos.discovery";

CalaosDiscovery::CalaosDiscovery():
    running_(false),
    discovering_(false),
    udp_listening_(false)
{
}

CalaosDiscovery::~CalaosDiscovery()
{
    stopDiscovery();
}

void CalaosDiscovery::startDiscovery()
{
    std::lock_guard<std::mutex> lock(discovery_mutex_);

    if (discovering_.load())
    {
        ESP_LOGW(TAG, "Discovery already running");
        return;
    }

    // Check for forced server IP from environment variable
    const char* forcedIp = std::getenv("CALAOS_SERVER_IP");
    if (forcedIp && strlen(forcedIp) > 0)
    {
        ESP_LOGI(TAG, "Using forced server IP from CALAOS_SERVER_IP: %s", forcedIp);

        // Initialize CalaosNet for HTTP requests (provisioning, websocket, etc.)
        if (!CalaosNet::instance().isInitialized())
        {
            NetworkResult result = CalaosNet::instance().init();
            if (result != NetworkResult::OK)
            {
                ESP_LOGE(TAG, "Failed to initialize CalaosNet");
                AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryTimeout));
                return;
            }
        }

        // Dispatch discovery started event
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryStarted));

        // Dispatch server found event with forced IP
        CalaosServerFoundData serverData;
        serverData.serverIp = forcedIp;
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosServerFound, serverData));

        // Dispatch discovery stopped event
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryStopped));
        return;
    }

    ESP_LOGI(TAG, "Starting Calaos server discovery");

    // Initialize CalaosNet if not already done
    if (!CalaosNet::instance().isInitialized())
    {
        NetworkResult result = CalaosNet::instance().init();
        if (result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize CalaosNet");
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryTimeout));
            return;
        }
    }

    discovering_.store(true);
    running_.store(true);

    discovery_start_time_ = std::chrono::steady_clock::now();
    last_broadcast_time_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(BROADCAST_INTERVAL_MS);

    // Start UDP listening for responses
    auto& udpClient = CalaosNet::instance().udpClient();
    NetworkResult result = udpClient.startReceiving(BCAST_UDP_PORT,
        [this](NetworkResult result, const NetworkBuffer& data)
        {
            onUdpDataReceived(result, data);
        });

    if (result != NetworkResult::OK)
    {
        ESP_LOGE(TAG, "Failed to start UDP listening on port %d", BCAST_UDP_PORT);
        discovering_.store(false);
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryTimeout));
        return;
    }

    udp_listening_ = true;

    // Start discovery thread
    discovery_thread_ = std::thread(&CalaosDiscovery::discoveryThread, this);

    // Dispatch discovery started event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryStarted));
}

void CalaosDiscovery::stopDiscovery()
{
    std::lock_guard<std::mutex> lock(discovery_mutex_);

    if (!discovering_.load())
    {
        return;
    }

    ESP_LOGI(TAG, "Stopping Calaos server discovery");

    running_.store(false);
    discovering_.store(false);

    // Stop UDP listening
    if (udp_listening_)
    {
        CalaosNet::instance().udpClient().stopReceiving();
        udp_listening_ = false;
    }

    // Wait for discovery thread to finish
    if (discovery_thread_.joinable())
    {
        discovery_thread_.join();
    }

    // Dispatch discovery stopped event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryStopped));
}

bool CalaosDiscovery::isDiscovering() const
{
    return discovering_.load();
}

void CalaosDiscovery::discoveryThread()
{
    ESP_LOGD(TAG, "Discovery thread started");

    while (running_.load() && discovering_.load())
    {
        auto current_time = std::chrono::steady_clock::now();

        // Check for discovery timeout
        auto discovery_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - discovery_start_time_);

        if (discovery_duration.count() > DISCOVERY_TIMEOUT_MS)
        {
            ESP_LOGW(TAG, "Discovery timeout reached");
            onDiscoveryTimeout();
            break;
        }

        // Send broadcast at regular intervals
        auto broadcast_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_broadcast_time_);

        if (broadcast_duration.count() >= BROADCAST_INTERVAL_MS && discovering_.load())
        {
            sendDiscoveryBroadcast();
            last_broadcast_time_ = current_time;
        }

        // Sleep for a reasonable time to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ESP_LOGD(TAG, "Discovery thread terminated");
}

void CalaosDiscovery::sendDiscoveryBroadcast()
{
    const char* discovery_msg = "CALAOS_DISCOVER";
    NetworkBuffer buffer(discovery_msg, strlen(discovery_msg));

    auto& udpClient = CalaosNet::instance().udpClient();
    NetworkResult result = udpClient.sendBroadcast(BCAST_UDP_PORT, buffer);

    if (result != NetworkResult::OK)
    {
        ESP_LOGE(TAG, "Failed to send discovery broadcast");
    }
}

void CalaosDiscovery::onUdpDataReceived(NetworkResult result, const NetworkBuffer& data)
{
    if (result != NetworkResult::OK)
    {
        ESP_LOGW(TAG, "UDP receive error");
        return;
    }

    // Quick exit if not discovering to avoid processing when stopped
    if (!discovering_.load())
    {
        return;
    }

    if (data.size < 9)
    {
        ESP_LOGD(TAG, "Received UDP packet too small (%zu bytes)", data.size);
        return;
    }

    // Convert to string for easier processing
    std::string message(reinterpret_cast<const char*>(data.data.data()), data.size);

    // Filter out our own broadcast messages
    if (message.substr(0, 15) == "CALAOS_DISCOVER")
        return;

    // Check if message starts with "CALAOS_IP"
    if (message.substr(0, 9) != "CALAOS_IP")
    {
        ESP_LOGD(TAG, "Received non-Calaos UDP message: %s", message.substr(0, 20).c_str());
        return;
    }

    // Extract IP address from message (after "CALAOS_IP")
    if (message.length() <= 9)
    {
        ESP_LOGW(TAG, "CALAOS_IP message without IP address");
        return;
    }

    std::string serverIp = message.substr(9);

    // Trim leading/trailing whitespace from IP
    size_t start = serverIp.find_first_not_of(" \t\r\n");
    size_t end = serverIp.find_last_not_of(" \t\r\n");
    if (start != std::string::npos && end != std::string::npos)
    {
        serverIp = serverIp.substr(start, end - start + 1);
    }

    // Basic IP validation (simple check for dots)
    if (serverIp.empty() || serverIp.find('.') == std::string::npos)
    {
        ESP_LOGW(TAG, "Invalid IP address in CALAOS_IP message: %s", serverIp.c_str());
        return;
    }

    ESP_LOGI(TAG, "Discovered Calaos server at: %s", serverIp.c_str());

    // Dispatch server found event
    CalaosServerFoundData serverData;
    serverData.serverIp = serverIp;
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosServerFound, serverData));

    // Stop discovery once we found a server to avoid continuous processing
    ESP_LOGI(TAG, "Stopping discovery after finding server");
    discovering_.store(false);

    // Dispatch discovery stopped event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryStopped));
}

void CalaosDiscovery::onDiscoveryTimeout()
{
    ESP_LOGW(TAG, "Discovery timeout reached, no servers found");

    // Stop discovery
    discovering_.store(false);

    // Dispatch timeout event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::CalaosDiscoveryTimeout));
}
#include "calaos_net.h"
#include "logging.h"

static const char *TAG = "net";

CalaosNet& CalaosNet::instance()
{
    static CalaosNet instance;
    return instance;
}

CalaosNet::CalaosNet():
    initialized_(false)
{
}

CalaosNet::~CalaosNet()
{
    cleanup();
}

UdpClient& CalaosNet::udpClient()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (!udp_client_)
    {
        udp_client_ = std::make_unique<UdpClient>();

        if (initialized_)
        {
            NetworkResult result = udp_client_->init();
            if (result != NetworkResult::OK)
            {
                ESP_LOGE(TAG, "Failed to initialize UDP client in CalaosNet");
            }
            else
            {
                udp_client_->setErrorCallback(
                    [this](NetworkResult error, const std::string& message)
                    {
                        globalErrorHandler(error, "UDP Client: " + message);
                    });
            }
        }
    }

    return *udp_client_;
}

UdpServer& CalaosNet::udpServer()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (!udp_server_)
    {
        udp_server_ = std::make_unique<UdpServer>();

        if (initialized_)
        {
            NetworkResult result = udp_server_->init();
            if (result != NetworkResult::OK)
            {
                ESP_LOGE(TAG, "Failed to initialize UDP server in CalaosNet");
            }
            else
            {
                udp_server_->setErrorCallback(
                    [this](NetworkResult error, const std::string& message)
                    {
                        globalErrorHandler(error, "UDP Server: " + message);
                    });
            }
        }
    }

    return *udp_server_;
}

HttpClient& CalaosNet::httpClient()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (!http_client_)
    {
        http_client_ = std::make_unique<HttpClient>();

        if (initialized_)
        {
            NetworkResult result = http_client_->init();
            if (result != NetworkResult::OK)
            {
                ESP_LOGE(TAG, "Failed to initialize HTTP client in CalaosNet");
            }
            else
            {
                http_client_->setErrorCallback(
                    [this](NetworkResult error, const std::string& message)
                    {
                        globalErrorHandler(error, "HTTP Client: " + message);
                    });
            }
        }
    }

    return *http_client_;
}

WebSocketClient& CalaosNet::webSocketClient()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (!websocket_client_)
    {
        websocket_client_ = std::make_unique<WebSocketClient>();

        if (initialized_)
        {
            NetworkResult result = websocket_client_->init();
            if (result != NetworkResult::OK)
            {
                ESP_LOGE(TAG, "Failed to initialize WebSocket client in CalaosNet");
            }
            else
            {
                websocket_client_->setErrorCallback(
                    [this](NetworkResult error, const std::string& message)
                    {
                        globalErrorHandler(error, "WebSocket Client: " + message);
                    });
            }
        }
    }

    return *websocket_client_;
}

NetworkResult CalaosNet::init()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (initialized_)
    {
        ESP_LOGW(TAG, "CalaosNet already initialized");
        return NetworkResult::OK;
    }

    ESP_LOGI(TAG, "Initializing CalaosNet network stack");

    NetworkResult result = NetworkResult::OK;

    if (udp_client_)
    {
        NetworkResult udp_result = udp_client_->init();
        if (udp_result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize UDP client");
            result = udp_result;
        }
        else
        {
            udp_client_->setErrorCallback(
                [this](NetworkResult error, const std::string& message)
                {
                    globalErrorHandler(error, "UDP Client: " + message);
                });
        }
    }

    if (udp_server_)
    {
        NetworkResult udp_server_result = udp_server_->init();
        if (udp_server_result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize UDP server");
            result = udp_server_result;
        }
        else
        {
            udp_server_->setErrorCallback(
                [this](NetworkResult error, const std::string& message)
                {
                    globalErrorHandler(error, "UDP Server: " + message);
                });
        }
    }

    if (http_client_)
    {
        NetworkResult http_result = http_client_->init();
        if (http_result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize HTTP client");
            result = http_result;
        }
        else
        {
            http_client_->setErrorCallback(
                [this](NetworkResult error, const std::string& message)
                {
                    globalErrorHandler(error, "HTTP Client: " + message);
                });
        }
    }

    if (websocket_client_)
    {
        NetworkResult ws_result = websocket_client_->init();
        if (ws_result != NetworkResult::OK)
        {
            ESP_LOGE(TAG, "Failed to initialize WebSocket client");
            result = ws_result;
        }
        else
        {
            websocket_client_->setErrorCallback(
                [this](NetworkResult error, const std::string& message)
                {
                    globalErrorHandler(error, "WebSocket Client: " + message);
                });
        }
    }

    if (result == NetworkResult::OK)
    {
        initialized_ = true;
        ESP_LOGI(TAG, "CalaosNet network stack initialized successfully");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to initialize CalaosNet network stack");
        cleanup();
    }

    return result;
}

void CalaosNet::cleanup()
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (!initialized_)
    {
        return;
    }

    ESP_LOGI(TAG, "Cleaning up CalaosNet network stack");

    if (websocket_client_)
    {
        websocket_client_->cleanup();
        websocket_client_.reset();
    }

    if (http_client_)
    {
        http_client_->cleanup();
        http_client_.reset();
    }

    if (udp_server_)
    {
        udp_server_->cleanup();
        udp_server_.reset();
    }

    if (udp_client_)
    {
        udp_client_->cleanup();
        udp_client_.reset();
    }

    initialized_ = false;
    ESP_LOGI(TAG, "CalaosNet network stack cleaned up");
}

bool CalaosNet::isInitialized() const
{
    std::lock_guard<std::mutex> lock(init_mutex_);
    return initialized_;
}

void CalaosNet::setGlobalErrorCallback(NetworkErrorCallback callback)
{
    global_error_callback_ = callback;
}

void CalaosNet::globalErrorHandler(NetworkResult error, const std::string& message)
{
    ESP_LOGE(TAG, "CalaosNet global error: %s", message.c_str());

    if (global_error_callback_)
    {
        global_error_callback_(error, message);
    }
}
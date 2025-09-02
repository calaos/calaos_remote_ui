#include "app_store.h"
#include "app_dispatcher.h"
#include "logging.h"

static const char* TAG = "AppStore";

AppStore::AppStore()
{
    // Subscribe to dispatcher to receive events
    AppDispatcher::getInstance().subscribe([this](const AppEvent& event)
    {
        handleEvent(event);
    });
}

AppStore& AppStore::getInstance()
{
    static AppStore instance;
    return instance;
}

const AppState& AppStore::getState() const
{
    flux::LockGuard lock(mutex_);
    return state_;
}

void AppStore::subscribe(StateChangeCallback callback)
{
    flux::LockGuard lock(mutex_);
    subscribers_.push_back(callback);
}

void AppStore::handleEvent(const AppEvent& event)
{
    ESP_LOGD(TAG, "Handling event type: %d", static_cast<int>(event.getType()));

    bool stateChanged = false;
    AppState previousState;

    {
        flux::LockGuard lock(mutex_);
        previousState = state_;

        switch (event.getType())
        {
            case AppEventType::NetworkStatusChanged:
            {
                if (auto* data = event.getData<NetworkStatusChangedData>())
                {
                    state_.network.isConnected = data->isConnected;
                    state_.network.connectionType = data->connectionType;
                    stateChanged = true;
                }
                break;
            }

            case AppEventType::NetworkIpAssigned:
            {
                if (auto* data = event.getData<NetworkIpAssignedData>())
                {
                    state_.network.isReady = true;
                    state_.network.isConnected = true;
                    state_.network.hasTimeout = false;
                    state_.network.connectionType = data->connectionType;
                    state_.network.ipAddress = data->ipAddress;
                    state_.network.gateway = data->gateway;
                    state_.network.netmask = data->netmask;
                    state_.network.ssid = data->ssid;
                    state_.network.rssi = data->rssi;
                    stateChanged = true;
                }
                break;
            }

            case AppEventType::NetworkDisconnected:
            {
                state_.network.isConnected = false;
                state_.network.isReady = false;
                state_.network.hasTimeout = false;
                state_.network.connectionType = NetworkConnectionType::None;
                state_.network.ipAddress.clear();
                state_.network.gateway.clear();
                state_.network.netmask.clear();
                state_.network.ssid.clear();
                state_.network.rssi = 0;
                stateChanged = true;
                break;
            }

            case AppEventType::NetworkTimeout:
            {
                ESP_LOGD(TAG, "Network timeout event received, isReady=%d, isConnected=%d", 
                         state_.network.isReady, state_.network.isConnected);
                // Only set timeout if no network connection is established at all
                if (!state_.network.isConnected)
                {
                    state_.network.hasTimeout = true;
                    stateChanged = true;
                    ESP_LOGD(TAG, "Setting hasTimeout=true, stateChanged=true");
                }
                else
                {
                    ESP_LOGD(TAG, "Network timeout ignored - already connected via %s", 
                             state_.network.connectionType == NetworkConnectionType::Ethernet ? "Ethernet" : "WiFi");
                }
                break;
            }
            
            case AppEventType::CalaosDiscoveryStarted:
            {
                state_.calaosServer.isDiscovering = true;
                state_.calaosServer.hasTimeout = false;
                stateChanged = true;
                ESP_LOGD(TAG, "Calaos discovery started");
                break;
            }
            
            case AppEventType::CalaosServerFound:
            {
                if (auto* data = event.getData<CalaosServerFoundData>())
                {
                    state_.calaosServer.addServer(data->serverIp);
                    stateChanged = true;
                    ESP_LOGD(TAG, "Calaos server found: %s", data->serverIp.c_str());
                }
                break;
            }
            
            case AppEventType::CalaosDiscoveryTimeout:
            {
                state_.calaosServer.isDiscovering = false;
                state_.calaosServer.hasTimeout = true;
                stateChanged = true;
                ESP_LOGD(TAG, "Calaos discovery timeout");
                break;
            }
            
            case AppEventType::CalaosDiscoveryStopped:
            {
                state_.calaosServer.isDiscovering = false;
                stateChanged = true;
                ESP_LOGD(TAG, "Calaos discovery stopped");
                break;
            }
            
            case AppEventType::ProvisioningCodeGenerated:
            {
                if (auto* data = event.getData<ProvisioningCodeGeneratedData>())
                {
                    state_.provisioning.status = ProvisioningStatus::ShowingCode;
                    state_.provisioning.provisioningCode = data->provisioningCode;
                    state_.provisioning.macAddress = data->macAddress;
                    state_.provisioning.hasFailed = false;
                    stateChanged = true;
                    ESP_LOGD(TAG, "Provisioning code generated: %s", data->provisioningCode.c_str());
                }
                break;
            }
            
            case AppEventType::ProvisioningCompleted:
            {
                if (auto* data = event.getData<ProvisioningCompletedData>())
                {
                    state_.provisioning.status = ProvisioningStatus::Provisioned;
                    state_.provisioning.deviceId = data->deviceId;
                    state_.provisioning.serverUrl = data->serverUrl;
                    state_.provisioning.hasFailed = false;
                    stateChanged = true;
                    ESP_LOGD(TAG, "Provisioning completed: %s", data->deviceId.c_str());
                }
                break;
            }
            
            case AppEventType::ProvisioningFailed:
            {
                state_.provisioning.hasFailed = true;
                stateChanged = true;
                ESP_LOGD(TAG, "Provisioning failed");
                break;
            }
        }
    }

    if (stateChanged && state_ != previousState)
    {
        ESP_LOGD(TAG, "State changed, notifying subscribers");
        notifyStateChange();
    }
    else if (stateChanged)
    {
        ESP_LOGD(TAG, "stateChanged=true but state == previousState");
    }
}

void AppStore::notifyStateChange()
{
    flux::LockGuard lock(mutex_);
    for (auto& callback : subscribers_)
        callback(state_);
}

void AppStore::clearSubscribers()
{
    flux::LockGuard lock(mutex_);
    subscribers_.clear();
}
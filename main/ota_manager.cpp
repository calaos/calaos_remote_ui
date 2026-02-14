/**
 * @file ota_manager.cpp
 * @brief OTA update manager implementation
 */

#include "ota_manager.h"
#include "logging.h"
#include "version.h"
#include "../flux/app_dispatcher.h"
#include "provisioning_manager.h"
#include "hmac_authenticator.h"
#include "calaos_protocol.h"

static const char* TAG = "ota.manager";

OtaManager::OtaManager()
{
}

OtaManager::~OtaManager()
{
    if (subscriptionId_ != 0)
    {
        AppStore::getInstance().unsubscribe(subscriptionId_);
    }
}

OtaManager& OtaManager::getInstance()
{
    static OtaManager instance;
    return instance;
}

bool OtaManager::init()
{
    if (initialized_)
    {
        return true;
    }

    ESP_LOGI(TAG, "Initializing OTA manager");

    // Create the platform-specific OTA implementation
    ota_ = createOta();
    if (!ota_)
    {
        ESP_LOGE(TAG, "Failed to create OTA implementation");
        return false;
    }

    if (!ota_->isSupported())
    {
        ESP_LOGW(TAG, "OTA not supported on this platform");
        // Still mark as initialized, just won't do anything
    }
    else
    {
        ESP_LOGI(TAG, "OTA backend initialized and supported");
    }

    // Subscribe to app state changes to detect OTA update events
    subscriptionId_ = AppStore::getInstance().subscribe(
        [this](const AppState& state)
        {
            onStateChanged(state);
        });

    initialized_ = true;
    return true;
}

bool OtaManager::startUpdate(const std::string& url, const std::string& checksum, const std::string& version)
{
    if (!initialized_ || !ota_)
    {
        ESP_LOGE(TAG, "OTA manager not initialized");
        return false;
    }

    if (!ota_->isSupported())
    {
        ESP_LOGW(TAG, "OTA not supported on this platform");
        return false;
    }

    if (updateInProgress_)
    {
        ESP_LOGW(TAG, "OTA update already in progress");
        return false;
    }

    ESP_LOGI(TAG, "Starting OTA update to version %s", version.c_str());
    ESP_LOGI(TAG, "Download URL: %s", url.c_str());

    currentVersion_ = version;
    updateInProgress_ = true;

    // Build authentication headers (same as WebSocket authentication)
    std::map<std::string, std::string> authHeaders = buildAuthHeaders();

    // Dispatch download started event
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaDownloadStarted));

    // Start the update with progress callback
    HalResult result = ota_->startUpdate(url, checksum, authHeaders,
        [this](const HalOtaProgress& progress)
        {
            onOtaProgress(progress);
        });

    if (result != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to start OTA update");
        updateInProgress_ = false;

        OtaErrorData errorData;
        errorData.errorMessage = "Failed to start OTA update";
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaError, errorData));

        return false;
    }

    return true;
}

bool OtaManager::abortUpdate()
{
    if (!initialized_ || !ota_)
    {
        return false;
    }

    if (!updateInProgress_)
    {
        return false;
    }

    ESP_LOGW(TAG, "Aborting OTA update");
    HalResult result = ota_->abortUpdate();

    if (result == HalResult::OK)
    {
        updateInProgress_ = false;
        return true;
    }

    return false;
}

bool OtaManager::isUpdateInProgress() const
{
    return updateInProgress_;
}

bool OtaManager::isSupported() const
{
    return ota_ && ota_->isSupported();
}

void OtaManager::onStateChanged(const AppState& state)
{
    // Check if a new OTA update became available
    if (state.ota.status == OtaStatus::Available && state.ota.updateAvailable && !updateInProgress_)
    {
        // Guard: skip if proposed version matches the currently installed version
        if (state.ota.version == APP_VERSION)
        {
            ESP_LOGW(TAG, "OTA version %s is already installed, ignoring update", state.ota.version.c_str());
            return;
        }

        ESP_LOGI(TAG, "OTA update available detected, version: %s", state.ota.version.c_str());

        // Automatically start the update (as per requirements - no user intervention)
        startUpdate(state.ota.downloadUrl, state.ota.checksumSha256, state.ota.version);
    }

    // Reset our internal state when OTA is reset (e.g., after error)
    if (state.ota.status == OtaStatus::Idle && updateInProgress_)
    {
        ESP_LOGI(TAG, "OTA state reset, clearing update in progress flag");
        updateInProgress_ = false;
    }
}

void OtaManager::onOtaProgress(const HalOtaProgress& progress)
{
    // Convert HAL progress to Flux events
    switch (progress.state)
    {
        case HalOtaState::Downloading:
        {
            OtaProgressData data;
            data.percent = progress.percent;
            data.bytesDownloaded = progress.bytesDownloaded;
            data.totalBytes = progress.totalBytes;
            data.status = "downloading";
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaProgress, data));
            break;
        }

        case HalOtaState::Installing:
        {
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaInstalling));
            break;
        }

        case HalOtaState::Rebooting:
        {
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaComplete));
            break;
        }

        case HalOtaState::Error:
        {
            updateInProgress_ = false;
            OtaErrorData errorData;
            errorData.errorMessage = progress.errorMessage;
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaError, errorData));
            break;
        }

        case HalOtaState::Idle:
        default:
            break;
    }
}

std::map<std::string, std::string> OtaManager::buildAuthHeaders()
{
    ProvisioningManager& provMgr = getProvisioningManager();

    std::string authToken = provMgr.getAuthToken();
    std::string deviceSecret = provMgr.getDeviceSecret();

    // Generate timestamp and nonce
    uint64_t timestamp = HMACAuthenticator::getTimestamp();
    std::string nonce = HMACAuthenticator::generateNonce();

    // Compute HMAC: HMAC-SHA256(device_secret, auth_token + ":" + timestamp + ":" + nonce)
    std::string dataToSign = authToken + ":" + std::to_string(timestamp) + ":" + nonce;
    std::string hmac = HMACAuthenticator::computeHmacSha256(deviceSecret, dataToSign);

    ESP_LOGD(TAG, "OTA Auth - Token: %s", authToken.c_str());
    ESP_LOGD(TAG, "OTA Auth - Timestamp: %lu", (unsigned long)timestamp);
    ESP_LOGD(TAG, "OTA Auth - Nonce: %s", nonce.c_str());

    std::map<std::string, std::string> headers;
    headers[CalaosProtocol::AUTH_HEADER_TOKEN] = "Bearer " + authToken;
    headers[CalaosProtocol::AUTH_HEADER_TIMESTAMP] = std::to_string(timestamp);
    headers[CalaosProtocol::AUTH_HEADER_NONCE] = nonce;
    headers[CalaosProtocol::AUTH_HEADER_HMAC] = hmac;

    return headers;
}

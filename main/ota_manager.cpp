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
#include "../hal/hal.h"

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

    // The store ignores an offer received during a download; the retry must
    // still use it, the download URL serves the newest image.
    AppDispatcher::getInstance().subscribe(AppEventType::OtaUpdateAvailable,
        [this](const AppEvent& event)
        {
            if (auto* data = event.getData<OtaUpdateAvailableData>())
                onOfferEvent(*data);
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
        // A pending offer holds the main page back: drop it
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaReset));
        return false;
    }

    bool expected = false;
    if (!updateInProgress_.compare_exchange_strong(expected, true))
    {
        ESP_LOGW(TAG, "OTA update already in progress");
        return false;
    }

    ESP_LOGI(TAG, "Starting OTA update to version %s", version.c_str());
    ESP_LOGI(TAG, "Download URL: %s", url.c_str());

    currentVersion_ = version;

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
        onOtaProgress(HalOtaProgress{HalOtaState::Error, 0, 0, 0, "Failed to start OTA update"});
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
    serverConnected_ = state.websocket.isConnected;
    observedStatus_ = static_cast<int>(state.ota.status);

    if (state.ota.status == OtaStatus::Available && state.ota.updateAvailable && !updateInProgress_)
    {
        // Guard: skip if proposed version matches the currently installed version
        if (state.ota.version == APP_VERSION)
        {
            ESP_LOGW(TAG, "OTA version %s is already installed, ignoring update", state.ota.version.c_str());
            return;
        }

        if (state.ota.checksumSha256.empty())
        {
            ESP_LOGE(TAG, "OTA offer v%s has no checksum, refused", state.ota.version.c_str());
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaReset));
            return;
        }

        ESP_LOGI(TAG, "OTA update available detected, version: %s", state.ota.version.c_str());

        {
            std::lock_guard<std::mutex> lock(retryMutex_);
            if (hasOffer_ && (lastOffer_.version != state.ota.version ||
                              lastOffer_.checksumSha256 != state.ota.checksumSha256))
                retry_.reset();
            lastOffer_.version = state.ota.version;
            lastOffer_.downloadUrl = state.ota.downloadUrl;
            lastOffer_.checksumSha256 = state.ota.checksumSha256;
            lastOffer_.releaseNotes = state.ota.releaseNotes;
            hasOffer_ = true;
            // A fresh offer is the server saying it is ready: no need to wait
            retry_.consume();
        }

        // Automatically start the update (as per requirements - no user intervention)
        startUpdate(state.ota.downloadUrl, state.ota.checksumSha256, state.ota.version);
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
            uint64_t retryAt = 0;
            {
                std::lock_guard<std::mutex> lock(retryMutex_);
                // A permanent failure waits for the server to offer again
                if (progress.permanent)
                {
                    retry_.consume();
                }
                else
                {
                    retry_.onFailure(HAL::getInstance().getSystem().getTimeMs());
                    retryAt = retry_.nextAttemptMs();
                }
            }
            updateInProgress_ = false;
            if (progress.permanent)
                ESP_LOGE(TAG, "OTA failed (%s), no retry until the server offers again",
                         progress.errorMessage.c_str());
            else
                ESP_LOGW(TAG, "OTA failed (%s), retry at t=%llu ms", progress.errorMessage.c_str(),
                         (unsigned long long)retryAt);
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

void OtaManager::onOfferEvent(const OtaUpdateAvailableData& offer)
{
    if (offer.version == APP_VERSION || offer.checksumSha256.empty() || offer.downloadUrl.empty())
        return;

    std::lock_guard<std::mutex> lock(retryMutex_);
    if (!hasOffer_ || lastOffer_.version != offer.version || lastOffer_.checksumSha256 != offer.checksumSha256)
        retry_.reset();
    lastOffer_ = offer;
    hasOffer_ = true;
}

void OtaManager::tick(uint64_t nowMs)
{
    // Every way out of a running status is an event; the dispatcher drops
    // events when its queue is full, and the update screen would then stay.
    const OtaStatus status = static_cast<OtaStatus>(observedStatus_.load());
    if ((status == OtaStatus::Downloading || status == OtaStatus::Installing) && !updateInProgress_)
    {
        if (!stuckTracking_)
        {
            stuckTracking_ = true;
            stuckSinceMs_ = nowMs;
        }
        else if (nowMs - stuckSinceMs_ >= kLostEventMs)
        {
            ESP_LOGE(TAG, "OTA status left running without a backend, ending it");
            stuckTracking_ = false;
            AppDispatcher::getInstance().dispatch(
                AppEvent(AppEventType::OtaError, OtaErrorData{"Update interrupted"}));
        }
    }
    else
    {
        stuckTracking_ = false;
    }

    OtaUpdateAvailableData offer;
    {
        std::lock_guard<std::mutex> lock(retryMutex_);
        if (!hasOffer_ || !retry_.due(nowMs) || updateInProgress_ || !serverConnected_)
            return;
        retry_.consume();
        offer = lastOffer_;
    }

    ESP_LOGI(TAG, "Retrying OTA update to version %s", offer.version.c_str());
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaUpdateAvailable, offer));
}

bool OtaManager::retryScheduled() const
{
    std::lock_guard<std::mutex> lock(retryMutex_);
    return retry_.armed();
}

uint64_t OtaManager::nextRetryMs() const
{
    std::lock_guard<std::mutex> lock(retryMutex_);
    return retry_.nextAttemptMs();
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

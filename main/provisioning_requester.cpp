#include "provisioning_requester.h"
#include "provisioning_manager.h"
#include "calaos_net.h"
#include "logging.h"
#include "hal.h"
#include "version.h"
#include "nlohmann/json.hpp"
#include <sstream>

using json = nlohmann::json;

static const char* TAG = "provisioning.req";

ProvisioningRequester::ProvisioningRequester():
    running_(false),
    requesting_(false)
{
}

ProvisioningRequester::~ProvisioningRequester()
{
    stopRequesting();
}

bool ProvisioningRequester::startRequesting(const std::string& serverIp,
                                           const std::string& provisioningCode)
{
    std::lock_guard<std::mutex> lock(request_mutex_);

    if (requesting_.load())
    {
        ESP_LOGW(TAG, "Provisioning requests already running");
        return false;
    }

    if (serverIp.empty() || provisioningCode.empty())
    {
        ESP_LOGE(TAG, "Invalid parameters: serverIp=%s, code=%s",
                 serverIp.c_str(), provisioningCode.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Starting provisioning requests to server: %s with code: %s",
             serverIp.c_str(), provisioningCode.c_str());

    server_ip_ = serverIp;
    provisioning_code_ = provisioningCode;

    requesting_.store(true);
    running_.store(true);

    // Initialize last_request_time to trigger immediate first request
    last_request_time_ = std::chrono::steady_clock::now() -
                        std::chrono::milliseconds(REQUEST_INTERVAL_MS);

    // Start request thread
    request_thread_ = std::thread(&ProvisioningRequester::requestThread, this);

    return true;
}

void ProvisioningRequester::stopRequesting()
{
    {
        std::lock_guard<std::mutex> lock(request_mutex_);

        if (!requesting_.load())
        {
            return;
        }

        ESP_LOGI(TAG, "Stopping provisioning requests");

        running_.store(false);
        requesting_.store(false);
    }

    // Wait for request thread to finish (unlock mutex first to allow thread to exit)
    if (request_thread_.joinable())
    {
        // Give the thread some time to finish gracefully
        auto start = std::chrono::steady_clock::now();
        bool joined = false;

        while (!joined && std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check if thread has finished
            if (!requesting_.load() && !running_.load())
            {
                request_thread_.join();
                joined = true;
            }
        }

        if (!joined)
        {
            ESP_LOGW(TAG, "Provisioning request thread did not stop gracefully, detaching");
            request_thread_.detach();
        }
        else
        {
            ESP_LOGI(TAG, "Provisioning request thread stopped successfully");
        }
    }
}

bool ProvisioningRequester::isRequesting() const
{
    return requesting_.load();
}

void ProvisioningRequester::requestThread()
{
    ESP_LOGD(TAG, "Provisioning request thread started");

    while (running_.load() && requesting_.load())
    {
        auto current_time = std::chrono::steady_clock::now();

        // Send request at regular intervals
        auto request_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_request_time_);

        if (request_duration.count() >= REQUEST_INTERVAL_MS && requesting_.load())
        {
            sendProvisioningRequest();
            last_request_time_ = current_time;
        }

        // Sleep to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ESP_LOGD(TAG, "Provisioning request thread terminated");
}

void ProvisioningRequester::sendProvisioningRequest()
{
    // Check if still requesting before sending
    if (!requesting_.load())
    {
        ESP_LOGD(TAG, "Provisioning request cancelled before sending");
        return;
    }

    // Check if network is initialized
    if (!CalaosNet::instance().isInitialized())
    {
        ESP_LOGE(TAG, "Network not initialized, cannot send provisioning request");
        return;
    }

    // Build URL
    std::ostringstream url;
    url << "http://" << server_ip_ << ":" << SERVER_PORT << "/api/v3/provision/request";

    // Build request body
    std::string body = buildProvisioningRequestBody();

    ESP_LOGI(TAG, "Sending provisioning request to: %s", url.str().c_str());
    ESP_LOGD(TAG, "Request body: %s", body.c_str());

    // Prepare HTTP request
    HttpRequest request;
    request.method = HttpMethod::POST;
    request.url = url.str();
    request.headers["Content-Type"] = "application/json";
    request.body = NetworkBuffer(body.c_str(), body.length());
    request.timeout_ms = REQUEST_TIMEOUT_MS;
    request.verify_ssl = false;

    // Send request asynchronously
    NetworkResult result = CalaosNet::instance().httpClient().sendRequest(
        request,
        [this](const HttpResponse& response)
        {
            onHttpResponse(response);
        });

    if (result != NetworkResult::OK)
    {
        ESP_LOGE(TAG, "Failed to send provisioning request: %d", static_cast<int>(result));
    }
}

void ProvisioningRequester::onHttpResponse(const HttpResponse& response)
{
    // Quick exit if not requesting to avoid processing when stopped
    if (!requesting_.load())
    {
        return;
    }

    ESP_LOGI(TAG, "Received provisioning response: status=%d",
             static_cast<int>(response.status_code));

    if (!response.isSuccess())
    {
        if (response.status_code == HttpStatus::NOT_FOUND)
        {
            ESP_LOGI(TAG, "Provisioning code not yet recognized by server, will retry...");
        }
        else
        {
            ESP_LOGW(TAG, "Provisioning request failed with status: %d - %s",
                    static_cast<int>(response.status_code),
                    response.error_message.c_str());
        }
        return;
    }

    // Parse response body
    try
    {
        std::string responseBody(reinterpret_cast<const char*>(response.body.data.data()),
                                response.body.size);

        ESP_LOGI(TAG, "Provisioning successful!");
        ESP_LOGD(TAG, "Response body: %s", responseBody.c_str());

        json j = json::parse(responseBody);

        // Extract provisioning data
        std::string status = j.value("status", "");
        if (status != "accepted")
        {
            ESP_LOGW(TAG, "Provisioning response status is not 'accepted': %s", status.c_str());
            ProvisioningFailedData data;
            data.errorMessage = "Server returned status: " + status;
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningFailed, data));
            return;
        }

        std::string deviceId = j.value("device_id", "");
        std::string authToken = j.value("auth_token", "");
        std::string deviceSecret = j.value("device_secret", "");

        // Extract server config
        auto serverConfig = j.value("server_config", json::object());
        std::string websocketUrl = serverConfig.value("websocket_url", "");
        std::string httpApiUrl = serverConfig.value("http_api_url", "");

        // Validate required fields
        if (deviceId.empty() || authToken.empty() || deviceSecret.empty())
        {
            ESP_LOGE(TAG, "Missing required fields in provisioning response");
            ProvisioningFailedData data;
            data.errorMessage = "Missing required fields in server response";
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningFailed, data));
            return;
        }

        ESP_LOGI(TAG, "Provisioning data received - device_id: %s", deviceId.c_str());

        // Stop requesting since we succeeded
        requesting_.store(false);

        // Complete provisioning through ProvisioningManager
        if (getProvisioningManager().completeProvisioning(deviceId, authToken,
                                                         deviceSecret, server_ip_))
        {
            ESP_LOGI(TAG, "Provisioning completed successfully!");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save provisioning configuration");
            ProvisioningFailedData data;
            data.errorMessage = "Failed to save provisioning configuration";
            AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningFailed, data));
        }
    }
    catch (const json::parse_error& e)
    {
        ESP_LOGE(TAG, "Failed to parse provisioning response JSON: %s", e.what());
        ProvisioningFailedData data;
        data.errorMessage = "Invalid JSON response from server";
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningFailed, data));
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Error processing provisioning response: %s", e.what());
        ProvisioningFailedData data;
        data.errorMessage = std::string("Error: ") + e.what();
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningFailed, data));
    }
}

std::string ProvisioningRequester::buildProvisioningRequestBody() const
{
    json j;

    // Add provisioning code
    j["code"] = provisioning_code_;

    // Add device info
    json deviceInfo = {
        {"type", "display"},
        {"manufacturer", "calaos"},
        {"model", HAL::getInstance().getSystem().getDeviceInfo()},
        {"version", APP_VERSION},
        {"mac_address", getProvisioningManager().getMacAddress()}
    };

    // Add capabilities
    deviceInfo["capabilities"] = json::parse(buildDeviceCapabilities());

    j["device_info"] = deviceInfo;

    return j.dump(0);
}

std::string ProvisioningRequester::buildDeviceCapabilities() const
{
    json capabilities;

    // Screen capabilities
    capabilities["screen"] = {
        {"width", 720},
        {"height", 720},
        {"touch", true},
        {"color_depth", 16}
    };

    // Network capabilities
    capabilities["network"] = {
        {"wifi", true},
        {"ethernet", true}
    };

    // Memory capabilities
    capabilities["memory"] = {
        {"ram", 33554432},      // 32MB
        {"storage", 16777216}   // 16MB
    };

    return capabilities.dump(0);
}

VerifyResult ProvisioningRequester::verifyProvisioning(const std::string& serverIp,
                                                       const std::string& deviceId,
                                                       const std::string& authToken)
{
    ESP_LOGI(TAG, "Verifying provisioning with server: %s for device: %s", serverIp.c_str(), deviceId.c_str());

    // Check if network is initialized
    if (!CalaosNet::instance().isInitialized())
    {
        ESP_LOGE(TAG, "Network not initialized, cannot verify provisioning");
        return VerifyResult::NetworkError;
    }

    // Build URL for dedicated verify endpoint
    std::ostringstream url;
    url << "http://" << serverIp << ":" << SERVER_PORT << "/api/v3/provision/verify";

    // Build verification request body with stored credentials (no provisioning code)
    json j;
    j["device_id"] = deviceId;
    j["auth_token"] = authToken;

    // Add device info for logging/analytics (no sensitive data)
    json deviceInfo = {
        {"type", "display"},
        {"manufacturer", "calaos"},
        {"model", HAL::getInstance().getSystem().getDeviceInfo()},
        {"version", APP_VERSION},
        {"mac_address", getProvisioningManager().getMacAddress()}
    };
    j["device_info"] = deviceInfo;

    std::string body = j.dump(0);

    uint32_t backoff_ms = VERIFY_INITIAL_BACKOFF_MS;

    for (int retry = 0; retry < VERIFY_MAX_RETRIES; retry++)
    {
        if (retry > 0)
        {
            ESP_LOGI(TAG, "Retrying provisioning verification (attempt %d/%d) after %ums",
                     retry + 1, VERIFY_MAX_RETRIES, backoff_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms *= 2;  // Exponential backoff
        }

        ESP_LOGI(TAG, "Sending provisioning verification request to: %s", url.str().c_str());

        // Prepare HTTP request
        HttpRequest request;
        request.method = HttpMethod::POST;
        request.url = url.str();
        request.headers["Content-Type"] = "application/json";
        request.body = NetworkBuffer(body.c_str(), body.length());
        request.timeout_ms = REQUEST_TIMEOUT_MS;
        request.verify_ssl = false;

        // Send synchronous request
        HttpResponse response;
        NetworkResult result = CalaosNet::instance().httpClient().sendRequestSync(request, response);

        if (result != NetworkResult::OK)
        {
            ESP_LOGW(TAG, "Verification request failed with network error: %d", static_cast<int>(result));
            continue;  // Retry on network error
        }

        ESP_LOGI(TAG, "Verification response: status=%d", static_cast<int>(response.status_code));

        // Check for authentication failure (invalid credentials)
        if (response.status_code == HttpStatus::UNAUTHORIZED ||
            response.status_code == HttpStatus::FORBIDDEN)
        {
            ESP_LOGW(TAG, "Provisioning verification failed: invalid credentials");
            return VerifyResult::InvalidCredentials;
        }

        // Check for success
        if (response.isSuccess())
        {
            // Parse response to verify it's a valid response
            try
            {
                std::string responseBody(reinterpret_cast<const char*>(response.body.data.data()),
                                        response.body.size);

                json j = json::parse(responseBody);
                std::string status = j.value("status", "");

                if (status == "valid")
                {
                    ESP_LOGI(TAG, "Provisioning verification successful");
                    return VerifyResult::Verified;
                }
                else if (status == "invalid")
                {
                    std::string reason = j.value("reason", "unknown");
                    ESP_LOGW(TAG, "Provisioning verification failed: %s", reason.c_str());
                    return VerifyResult::InvalidCredentials;
                }
                else
                {
                    ESP_LOGW(TAG, "Provisioning verification: unexpected status '%s'", status.c_str());
                    return VerifyResult::InvalidCredentials;
                }
            }
            catch (const std::exception& e)
            {
                ESP_LOGE(TAG, "Failed to parse verification response: %s", e.what());
                continue;  // Retry on parse error
            }
        }

        // For NOT_FOUND, the verify endpoint doesn't exist or device unknown
        if (response.status_code == HttpStatus::NOT_FOUND)
        {
            ESP_LOGW(TAG, "Device not found on server");
            return VerifyResult::InvalidCredentials;
        }

        // For other errors, retry
        ESP_LOGW(TAG, "Verification request failed with status: %d", static_cast<int>(response.status_code));
    }

    ESP_LOGE(TAG, "Provisioning verification failed after %d retries", VERIFY_MAX_RETRIES);
    return VerifyResult::NetworkError;
}

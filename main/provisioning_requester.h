#pragma once

#include "flux.h"
#include "network_types.h"
#include "http/http_types.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

/**
 * @brief Result of provisioning verification request
 */
enum class VerifyResult
{
    Verified,           // Server accepted the credentials
    InvalidCredentials, // Server rejected credentials (401/403)
    NetworkError        // Network/connection error
};

/**
 * @brief Handles provisioning request attempts to Calaos server
 *
 * This class manages the periodic HTTP POST requests to provision the device
 * with the Calaos server after discovery phase.
 */
class ProvisioningRequester
{
public:
    ProvisioningRequester();
    ~ProvisioningRequester();

    /**
     * @brief Start sending provisioning requests to the server
     * @param serverIp The IP address of the Calaos server
     * @param provisioningCode The 6-character provisioning code
     * @return true if started successfully
     */
    bool startRequesting(const std::string& serverIp, const std::string& provisioningCode);

    /**
     * @brief Stop sending provisioning requests
     */
    void stopRequesting();

    /**
     * @brief Check if currently requesting
     */
    bool isRequesting() const;

    /**
     * @brief Verify existing provisioning credentials with the server
     * @param serverIp The IP address of the Calaos server
     * @param deviceId The stored device ID
     * @param authToken The stored auth token
     * @return VerifyResult indicating success, invalid credentials, or network error
     *
     * This method performs synchronous HTTP requests to /api/v3/provision/verify
     * with retry logic: 3 retries with exponential backoff (1s, 2s, 4s) for network errors.
     * Uses stored credentials (device_id + auth_token) for secure verification.
     */
    VerifyResult verifyProvisioning(const std::string& serverIp,
                                    const std::string& deviceId,
                                    const std::string& authToken);

private:
    void requestThread();
    void sendProvisioningRequest();
    void onHttpResponse(const HttpResponse& response);
    std::string buildProvisioningRequestBody() const;
    std::string buildDeviceCapabilities() const;

    std::atomic<bool> running_;
    std::atomic<bool> requesting_;
    std::thread request_thread_;
    mutable std::mutex request_mutex_;

    std::string server_ip_;
    std::string provisioning_code_;
    std::chrono::steady_clock::time_point last_request_time_;

    static constexpr uint32_t REQUEST_INTERVAL_MS = 10000;  // 10 seconds
    static constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;    // 5 seconds
    static constexpr uint16_t SERVER_PORT = 5454;
    static constexpr int VERIFY_MAX_RETRIES = 3;
    static constexpr uint32_t VERIFY_INITIAL_BACKOFF_MS = 1000;  // 1 second
};

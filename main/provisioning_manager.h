#pragma once

#include "provisioning_crypto.h"
#include "flux.h"
#include <string>
#include <vector>
#include <memory>

// Configuration structure stored persistently
struct ProvisioningConfig
{
    std::string provisioningCode;
    std::vector<uint8_t> salt;
    bool provisioned = false;
    std::string deviceId;
    std::string authToken;
    std::string deviceSecret;
    std::string serverUrl;
    std::string macAddress;

    // Serialize to JSON string for storage
    std::string toJson() const;

    // Deserialize from JSON string
    bool fromJson(const std::string& json);

    // Check if provisioning is complete
    bool isComplete() const
    {
        return provisioned && !deviceId.empty() && !authToken.empty() &&
               !deviceSecret.empty() && !serverUrl.empty();
    }
};

class ProvisioningManager
{
public:
    ProvisioningManager();
    ~ProvisioningManager();

    // Initialize the provisioning manager
    bool init();

    // Check if device is already provisioned
    bool isProvisioned() const;

    // Get current provisioning code (generates one if needed)
    std::string getProvisioningCode();

    // Get device MAC address
    std::string getMacAddress() const;

    // Load provisioning config from persistent storage
    bool loadConfig();

    // Save provisioning config to persistent storage
    bool saveConfig();

    // Reset provisioning (for testing/factory reset)
    void resetProvisioning();

    // Get device info for provisioning request
    std::string getDeviceInfo() const;

    // Complete provisioning with server response data
    bool completeProvisioning(const std::string& deviceId,
                             const std::string& authToken,
                             const std::string& deviceSecret,
                             const std::string& serverUrl);

private:
    // Generate a new provisioning code
    std::string generateNewCode();

    // Generate device info JSON for provisioning request
    std::string generateDeviceInfoJson() const;

    ProvisioningConfig config_;

    static const char* STORAGE_KEY_PROVISIONING;
};

// Singleton access
ProvisioningManager& getProvisioningManager();
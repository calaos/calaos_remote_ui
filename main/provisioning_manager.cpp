#include "provisioning_manager.h"
#include "logging.h"
#include "app_dispatcher.h"
#include "hal.h"
#include "nlohmann/json.hpp"
#include <sstream>

using json = nlohmann::json;

static const char* TAG = "provisioning.manager";

const char* ProvisioningManager::STORAGE_KEY_PROVISIONING = "prov.config";

std::string ProvisioningConfig::toJson() const
{
    json j = {
        {"provisioning_code", provisioningCode},
        {"salt", ProvisioningCrypto::bytesToHexString(salt)},
        {"provisioned", provisioned},
        {"device_id", deviceId},
        {"auth_token", authToken},
        {"device_secret", deviceSecret},
        {"server_url", serverUrl},
        {"mac_address", macAddress}
    };

    return j.dump(2);
}

bool ProvisioningConfig::fromJson(const std::string& jsonStr)
{
    try
    {
        json j = json::parse(jsonStr);

        // Extract values with defaults for missing fields
        provisioningCode = j.value("provisioning_code", "");

        std::string saltHex = j.value("salt", "");
        salt = ProvisioningCrypto::hexStringToBytes(saltHex);

        provisioned = j.value("provisioned", false);
        deviceId = j.value("device_id", "");
        authToken = j.value("auth_token", "");
        deviceSecret = j.value("device_secret", "");
        serverUrl = j.value("server_url", "");
        macAddress = j.value("mac_address", "");

        return true;
    }
    catch (const json::parse_error& e)
    {
        ESP_LOGE(TAG, "Failed to parse JSON config: %s", e.what());
        return false;
    }
    catch (const json::type_error& e)
    {
        ESP_LOGE(TAG, "Invalid JSON type in config: %s", e.what());
        return false;
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Unexpected error parsing JSON config: %s", e.what());
        return false;
    }
}

ProvisioningManager::ProvisioningManager()
{
}

ProvisioningManager::~ProvisioningManager()
{
}

bool ProvisioningManager::init()
{
    ESP_LOGI(TAG, "Initializing provisioning manager");

    // Get MAC address first
    std::string macAddress = HAL::getInstance().getNetwork().getMacAddress();
    if (macAddress.empty())
    {
        ESP_LOGE(TAG, "Failed to get MAC address");
        return false;
    }

    ESP_LOGI(TAG, "Device MAC address: %s", macAddress.c_str());

    // Load existing configuration
    if (!loadConfig())
    {
        ESP_LOGW(TAG, "No existing provisioning config found, will generate new one");
        resetProvisioning();
    }
    
    // Always ensure MAC address is set correctly (loadConfig might have overwritten it)
    config_.macAddress = macAddress;

    // Dispatch initial provisioning state
    if (isProvisioned())
    {
        ProvisioningCompletedData data;
        data.deviceId = config_.deviceId;
        data.serverUrl = config_.serverUrl;
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningCompleted, data));
    }
    else
    {
        // Generate provisioning code and dispatch event
        std::string code = getProvisioningCode();
        ProvisioningCodeGeneratedData data;
        data.provisioningCode = code;
        data.macAddress = config_.macAddress;
        AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningCodeGenerated, data));
    }

    return true;
}

bool ProvisioningManager::isProvisioned() const
{
    return config_.isComplete();
}

std::string ProvisioningManager::getProvisioningCode()
{
    if (config_.provisioningCode.empty() ||
        config_.provisioningCode.starts_with("ERROR"))
    {
        ESP_LOGD(TAG, "getProvisioningCode must generate new code with mac address: %s", config_.macAddress.c_str());
        config_.provisioningCode = generateNewCode();
        saveConfig();
    }

    return config_.provisioningCode;
}

std::string ProvisioningManager::getMacAddress() const
{
    return config_.macAddress;
}

bool ProvisioningManager::loadConfig()
{
    std::string jsonStr;
    HalResult result = HAL::getInstance().getSystem().loadConfig(STORAGE_KEY_PROVISIONING, jsonStr);

    if (result != HalResult::OK)
    {
        ESP_LOGD(TAG, "No provisioning config found in storage");
        return false;
    }

    if (!config_.fromJson(jsonStr))
    {
        ESP_LOGE(TAG, "Failed to parse provisioning config JSON");
        return false;
    }

    ESP_LOGI(TAG, "Loaded provisioning config - provisioned: %s, code: %s",
             config_.provisioned ? "true" : "false",
             config_.provisioningCode.c_str());

    return true;
}

bool ProvisioningManager::saveConfig()
{
    std::string jsonStr = config_.toJson();
    HalResult result = HAL::getInstance().getSystem().saveConfig(STORAGE_KEY_PROVISIONING, jsonStr);

    if (result != HalResult::OK)
    {
        ESP_LOGE(TAG, "Failed to save provisioning config");
        return false;
    }

    ESP_LOGD(TAG, "Saved provisioning config");
    return true;
}

void ProvisioningManager::resetProvisioning()
{
    ESP_LOGI(TAG, "Resetting provisioning");

    config_.provisioningCode.clear();
    config_.salt.clear();
    config_.provisioned = false;
    config_.deviceId.clear();
    config_.authToken.clear();
    config_.deviceSecret.clear();
    config_.serverUrl.clear();

    // Generate new salt and code
    config_.salt = ProvisioningCrypto::generateRandomSalt(4);
    config_.provisioningCode = generateNewCode();

    saveConfig();
}

std::string ProvisioningManager::getDeviceInfo() const
{
    return generateDeviceInfoJson();
}

bool ProvisioningManager::completeProvisioning(const std::string& deviceId,
                                              const std::string& authToken,
                                              const std::string& deviceSecret,
                                              const std::string& serverUrl)
{
    ESP_LOGI(TAG, "Completing provisioning for device: %s", deviceId.c_str());

    config_.provisioned = true;
    config_.deviceId = deviceId;
    config_.authToken = authToken;
    config_.deviceSecret = deviceSecret;
    config_.serverUrl = serverUrl;

    if (!saveConfig())
    {
        ESP_LOGE(TAG, "Failed to save completed provisioning config");
        return false;
    }

    // Dispatch provisioning completed event
    ProvisioningCompletedData data;
    data.deviceId = deviceId;
    data.serverUrl = serverUrl;
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ProvisioningCompleted, data));

    return true;
}

std::string ProvisioningManager::generateNewCode()
{
    if (config_.salt.empty())
    {
        config_.salt = ProvisioningCrypto::generateRandomSalt(4);
    }

    ESP_LOGD(TAG, "generateNewCode with mac address: %s", config_.macAddress.c_str());

    return ProvisioningCrypto::generateProvisioningCode(config_.macAddress, config_.salt);
}

std::string ProvisioningManager::generateDeviceInfoJson() const
{
    json j = {
        {"model", HAL::getInstance().getSystem().getDeviceInfo()},
        {"firmware", HAL::getInstance().getSystem().getFirmwareVersion()},
        {"mac_address", config_.macAddress},
    };

    return j.dump(2);
}

// Singleton instance
static std::unique_ptr<ProvisioningManager> g_provisioningManager = nullptr;

ProvisioningManager& getProvisioningManager()
{
    if (!g_provisioningManager)
    {
        g_provisioningManager = std::make_unique<ProvisioningManager>();
    }
    return *g_provisioningManager;
}
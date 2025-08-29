#include "provisioning_crypto.h"
#include "logging.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <string.h>

static const char* TAG = "provisioning.crypto";

// Base32 alphabet without ambiguous characters (0/O, 1/I/L removed)
const char ProvisioningCrypto::BASE32_ALPHABET[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";

std::vector<uint8_t> ProvisioningCrypto::generateRandomSalt(size_t size)
{
    std::vector<uint8_t> salt(size);

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "calaos_provisioning_salt";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char*)pers, strlen(pers));

    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to seed random number generator: -0x%04x", -ret);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        return salt; // Return empty/zero salt on error
    }

    ret = mbedtls_ctr_drbg_random(&ctr_drbg, salt.data(), size);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to generate random salt: -0x%04x", -ret);
    }
    else
    {
        ESP_LOGD(TAG, "Generated %zu bytes random salt", size);
    }

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return salt;
}

std::vector<uint8_t> ProvisioningCrypto::calculateSHA256(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> hash(32); // SHA256 is 32 bytes

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data.data(), data.size());
    mbedtls_sha256_finish(&ctx, hash.data());
#else
    int ret = mbedtls_sha256_starts_ret(&ctx, 0);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "SHA256 hash failed to start");
        mbedtls_sha256_free(&ctx);
        return {};
    }

    ret = mbedtls_sha256_update_ret(&ctx, data.data(), data.size());
    if (ret != 0)
    {
        ESP_LOGE(TAG, "SHA256 hash failed to update");
        mbedtls_sha256_free(&ctx);
        return {};
    }

    ret = mbedtls_sha256_finish_ret(&ctx, hash.data());
    if (ret != 0)
    {
        ESP_LOGE(TAG, "SHA256 hash failed to finish");
        mbedtls_sha256_free(&ctx);
        return {};
    }
#endif
    ESP_LOGD(TAG, "Calculated SHA256 hash successfully");

    mbedtls_sha256_free(&ctx);
    return hash;
}

std::string ProvisioningCrypto::encodeBase32(const std::vector<uint8_t>& data, size_t maxLength)
{
    if (data.empty())
    {
        return "";
    }

    std::string result;
    result.reserve((data.size() * 8 + 4) / 5); // Base32 expansion ratio

    uint32_t buffer = 0;
    int bitsLeft = 0;

    for (uint8_t byte : data)
    {
        buffer = (buffer << 8) | byte;
        bitsLeft += 8;

        // Extract 5-bit groups
        while (bitsLeft >= 5)
        {
            int index = (buffer >> (bitsLeft - 5)) & 0x1F;
            result += BASE32_ALPHABET[index];
            bitsLeft -= 5;

            // Stop if we reached max length
            if (result.length() >= maxLength)
            {
                return result;
            }
        }
    }

    // Handle remaining bits
    if (bitsLeft > 0 && result.length() < maxLength)
    {
        int index = (buffer << (5 - bitsLeft)) & 0x1F;
        result += BASE32_ALPHABET[index];
    }

    // Truncate to maxLength if needed
    if (result.length() > maxLength)
    {
        result = result.substr(0, maxLength);
    }

    ESP_LOGD(TAG, "Base32 encoded to: %s", result.c_str());
    return result;
}

std::string ProvisioningCrypto::generateProvisioningCode(const std::string& macAddress,
                                                        const std::vector<uint8_t>& salt)
{
    // Convert MAC address to bytes
    std::vector<uint8_t> macBytes = hexStringToBytes(macAddress);
    if (macBytes.empty())
    {
        ESP_LOGE(TAG, "Failed to parse MAC address: %s", macAddress.c_str());
        return "ERROR1";
    }

    // Combine MAC + salt
    std::vector<uint8_t> combined;
    combined.reserve(macBytes.size() + salt.size());
    combined.insert(combined.end(), macBytes.begin(), macBytes.end());
    combined.insert(combined.end(), salt.begin(), salt.end());

    // Calculate SHA256
    std::vector<uint8_t> hash = calculateSHA256(combined);
    if (hash.empty())
    {
        ESP_LOGE(TAG, "Failed to calculate SHA256 hash");
        return "ERROR2";
    }

    // Take first 4 bytes and encode to base32
    std::vector<uint8_t> codeBytes(hash.begin(), hash.begin() + 4);
    std::string code = encodeBase32(codeBytes, 6);

    if (code.length() < 6)
    {
        // Pad with first characters if needed
        while (code.length() < 6)
        {
            code += BASE32_ALPHABET[0]; // '2'
        }
    }

    ESP_LOGI(TAG, "Generated provisioning code: %s", code.c_str());
    return code;
}

std::vector<uint8_t> ProvisioningCrypto::hexStringToBytes(const std::string& hexStr)
{
    std::vector<uint8_t> bytes;

    // Remove common separators
    std::string cleaned = hexStr;
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ':'), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '-'), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ' '), cleaned.end());

    // Convert to uppercase
    std::transform(cleaned.begin(), cleaned.end(), cleaned.begin(), ::toupper);

    if (cleaned.length() % 2 != 0)
    {
        ESP_LOGE(TAG, "Invalid hex string length: %s", hexStr.c_str());
        return bytes;
    }

    bytes.reserve(cleaned.length() / 2);

    for (size_t i = 0; i < cleaned.length(); i += 2)
    {
        std::string byteStr = cleaned.substr(i, 2);
        try
        {
            uint8_t byte = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
            bytes.push_back(byte);
        }
        catch (const std::exception& e)
        {
            ESP_LOGE(TAG, "Failed to parse hex byte: %s", byteStr.c_str());
            return std::vector<uint8_t>(); // Return empty on error
        }
    }

    return bytes;
}

std::string ProvisioningCrypto::bytesToHexString(const std::vector<uint8_t>& bytes)
{
    std::ostringstream oss;
    for (uint8_t byte : bytes)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}
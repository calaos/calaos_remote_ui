#include "hmac_authenticator.h"
#include "logging.h"
#include "mbedtls/md.h"
#include <sstream>
#include <iomanip>
#include <ctime>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#else
#include <random>
#endif

static const char* TAG = "HMACAuthenticator";

std::string HMACAuthenticator::computeHmacSha256(const std::string& key, const std::string& data)
{
    // Use mbedTLS for both ESP32 and Linux
    unsigned char output[32];
    mbedtls_md_context_t ctx;

    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr)
    {
        ESP_LOGE(TAG, "Failed to get MD info for SHA256");
        mbedtls_md_free(&ctx);
        return "";
    }

    if (mbedtls_md_setup(&ctx, md_info, 1) != 0)
    {
        ESP_LOGE(TAG, "Failed to setup MD context");
        mbedtls_md_free(&ctx);
        return "";
    }

    if (mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length()) != 0)
    {
        ESP_LOGE(TAG, "Failed to start HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }

    if (mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length()) != 0)
    {
        ESP_LOGE(TAG, "Failed to update HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }

    if (mbedtls_md_hmac_finish(&ctx, output) != 0)
    {
        ESP_LOGE(TAG, "Failed to finish HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }

    mbedtls_md_free(&ctx);

    return bytesToHex(output, 32);
}

std::string HMACAuthenticator::generateNonce()
{
    // Generate 32 random bytes (64 hex characters)
    unsigned char nonce[32];

#ifdef ESP_PLATFORM
    // ESP32: Use hardware RNG
    esp_fill_random(nonce, sizeof(nonce));
#else
    // Linux: Use std::random_device
    std::random_device rd;
    for (size_t i = 0; i < sizeof(nonce); i++)
        nonce[i] = static_cast<unsigned char>(rd() & 0xFF);
#endif

    return bytesToHex(nonce, sizeof(nonce));
}

uint64_t HMACAuthenticator::getTimestamp()
{
    return static_cast<uint64_t>(std::time(nullptr));
}

std::string HMACAuthenticator::bytesToHex(const unsigned char* data, size_t length)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (size_t i = 0; i < length; i++)
        oss << std::setw(2) << static_cast<int>(data[i]);

    return oss.str();
}

std::vector<uint8_t> HMACAuthenticator::hexToBytes(const std::string& hex)
{
    std::vector<uint8_t> bytes;

    for (size_t i = 0; i < hex.length(); i += 2)
    {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

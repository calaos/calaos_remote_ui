#pragma once

#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief HMAC-SHA256 authenticator for WebSocket authentication
 *
 * Provides HMAC-SHA256 computation using platform-specific crypto libraries:
 * - ESP32: mbedTLS
 * - Linux: OpenSSL
 */
class HMACAuthenticator
{
public:
    /**
     * @brief Compute HMAC-SHA256 signature
     * @param key Secret key for HMAC computation
     * @param data Data to sign
     * @return Hex-encoded HMAC signature (64 characters)
     */
    static std::string computeHmacSha256(const std::string& key, const std::string& data);

    /**
     * @brief Generate random nonce for authentication
     * @return Hex-encoded random nonce (64 characters)
     */
    static std::string generateNonce();

    /**
     * @brief Get current Unix timestamp
     * @return Current time in seconds since epoch
     */
    static uint64_t getTimestamp();

    /**
     * @brief Convert bytes to hex string
     * @param data Byte array
     * @param length Length of byte array
     * @return Hex-encoded string
     */
    static std::string bytesToHex(const unsigned char* data, size_t length);

    /**
     * @brief Convert hex string to bytes
     * @param hex Hex-encoded string
     * @return Byte vector
     */
    static std::vector<uint8_t> hexToBytes(const std::string& hex);
};

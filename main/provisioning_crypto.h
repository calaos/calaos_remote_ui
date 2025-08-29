#pragma once

#include <string>
#include <vector>
#include <cstdint>

class ProvisioningCrypto
{
public:
    // Generate a random salt of specified size
    static std::vector<uint8_t> generateRandomSalt(size_t size = 4);
    
    // Calculate SHA256 hash of input data
    static std::vector<uint8_t> calculateSHA256(const std::vector<uint8_t>& data);
    
    // Encode bytes to base32 using Calaos alphabet (no ambiguous chars)
    static std::string encodeBase32(const std::vector<uint8_t>& data, size_t maxLength = 6);
    
    // Generate provisioning code from MAC address and salt
    // Algorithm: SHA256(MAC_ADDRESS + SALT) -> base32 -> first 6 chars
    static std::string generateProvisioningCode(const std::string& macAddress, 
                                               const std::vector<uint8_t>& salt);
    
    // Helper to convert hex string to bytes (for MAC address)
    static std::vector<uint8_t> hexStringToBytes(const std::string& hexStr);
    
    // Helper to convert bytes to hex string
    static std::string bytesToHexString(const std::vector<uint8_t>& bytes);

private:
    // Base32 alphabet without ambiguous characters (0/O, 1/I/L)
    static const char BASE32_ALPHABET[];
};
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Binary header constants
constexpr uint8_t CFG_MAGIC_0 = 0x43; // 'C'
constexpr uint8_t CFG_MAGIC_1 = 0x41; // 'A'
constexpr uint8_t CFG_MAGIC_2 = 0x4C; // 'L'
constexpr uint8_t CFG_MAGIC_3 = 0x4F; // 'O'
constexpr uint8_t CFG_FORMAT_VERSION = 0x01;
constexpr size_t CFG_HEADER_SIZE = 12; // magic:4 + version:1 + flags:1 + json_len:2 + crc:4

enum class CfgError
{
    Ok = 0,
    NoMagic = -1,
    BadVersion = -2,
    BadCrc = -3,
    BadJson = -4,
    ReadError = -5,
};

struct CalaosConfig
{
    std::string networkInterface = "ethernet"; // "ethernet" or "wifi"
    std::string ipMode = "dhcp";               // "dhcp" or "static"
    std::string staticIp;
    std::string staticMask;
    std::string staticGateway;
    std::string staticDns;
    std::string wifiSsid;
    std::string wifiPassword;
    std::string serverHost;
    uint16_t serverPort = 5454;
    bool serverSsl = false;
    bool hasServerHost = false;
};

// Sequential byte reader abstraction.
// Each call to read() continues from where the previous one left off.
// Platform implementations provide concrete subclasses (partition reader,
// file reader, etc.).
class CfgReader
{
public:
    virtual ~CfgReader() = default;

    // Read exactly `len` bytes into `dst`.
    // Returns true on success, false on I/O error.
    virtual bool read(uint8_t *dst, size_t len) = 0;
};

// Parse a device-config binary image via a streaming reader.
// Reads only the header (12 bytes) then the exact JSON payload.
// On failure the output struct is left at defaults.
CfgError calaosConfigParse(CfgReader &reader, CalaosConfig &cfg);

// Serialize a config into the binary image format understood by
// calaosConfigParse(): 12-byte header {magic, version, flags, json_len, crc}
// followed by the JSON payload. Round-trips byte-exactly through
// calaosConfigParse().
// Returns true on success, false if the payload cannot be represented
// (e.g. JSON larger than 64KB).
bool calaosConfigSerialize(const CalaosConfig &cfg, std::vector<uint8_t> &out);

// CRC-32 (ISO 3309 / zlib compatible)
uint32_t calaosConfigCrc32(const uint8_t *data, size_t len);

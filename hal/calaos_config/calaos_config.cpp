#include "calaos_config.h"
#include "logging.h"
#include <nlohmann/json.hpp>
#include <vector>

static const char* TAG = "calaos_config";

// ============================================================================
// CRC-32 (ISO 3309 / ITU-T V.42) — table computed from polynomial 0xEDB88320
// ============================================================================

static const struct Crc32Table
{
    uint32_t entries[256];
    constexpr Crc32Table() : entries{}
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            entries[i] = crc;
        }
    }
} crc32Table;

uint32_t calaosConfigCrc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32Table.entries[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Streaming parse: reads header then payload via CfgReader
// ============================================================================

CfgError calaosConfigParse(CfgReader &reader, CalaosConfig &cfg)
{
    cfg = CalaosConfig{}; // reset to defaults

    // Step 1 — read header
    uint8_t header[CFG_HEADER_SIZE];
    if (!reader.read(header, CFG_HEADER_SIZE))
    {
        ESP_LOGE(TAG, "Failed to read %zu-byte header", CFG_HEADER_SIZE);
        return CfgError::ReadError;
    }

    ESP_LOGI(TAG, "Header bytes: %02X %02X %02X %02X  ver=%02X flags=%02X  len=%02X%02X  crc=%02X%02X%02X%02X",
             header[0], header[1], header[2], header[3],
             header[4], header[5],
             header[7], header[6],
             header[11], header[10], header[9], header[8]);

    // Step 2 — magic bytes
    if (header[0] != CFG_MAGIC_0 || header[1] != CFG_MAGIC_1 ||
        header[2] != CFG_MAGIC_2 || header[3] != CFG_MAGIC_3)
    {
        ESP_LOGE(TAG, "Bad magic: expected CALO (43 41 4C 4F), got %02X %02X %02X %02X",
                 header[0], header[1], header[2], header[3]);
        return CfgError::NoMagic;
    }

    // Step 3 — format version
    if (header[4] != CFG_FORMAT_VERSION)
    {
        ESP_LOGE(TAG, "Bad version: expected %02X, got %02X", CFG_FORMAT_VERSION, header[4]);
        return CfgError::BadVersion;
    }

    // header[5] = flags (reserved, ignored)

    // Step 4 — extract JSON payload length and expected CRC
    uint16_t jsonLen = static_cast<uint16_t>(header[6] | (header[7] << 8));
    uint32_t expectedCrc = static_cast<uint32_t>(
        header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24));

    ESP_LOGI(TAG, "JSON payload length: %u bytes, expected CRC: 0x%08X", jsonLen, expectedCrc);

    if (jsonLen == 0)
    {
        ESP_LOGE(TAG, "JSON payload length is 0");
        return CfgError::BadJson;
    }

    // Step 5 — read JSON payload
    std::vector<uint8_t> jsonBuf(jsonLen);
    if (!reader.read(jsonBuf.data(), jsonLen))
    {
        ESP_LOGE(TAG, "Failed to read %u-byte JSON payload", jsonLen);
        return CfgError::ReadError;
    }

    // Step 6 — verify CRC
    uint32_t actualCrc = calaosConfigCrc32(jsonBuf.data(), jsonLen);
    ESP_LOGI(TAG, "CRC check: expected=0x%08X actual=0x%08X %s",
             expectedCrc, actualCrc, (actualCrc == expectedCrc) ? "OK" : "MISMATCH");
    if (actualCrc != expectedCrc)
        return CfgError::BadCrc;

    // Step 7 — parse JSON with nlohmann/json
    std::string rawJson(jsonBuf.begin(), jsonBuf.end());
    ESP_LOGI(TAG, "Raw JSON payload: %s", rawJson.c_str());

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(jsonBuf.begin(), jsonBuf.end());
    }
    catch (const nlohmann::json::parse_error &e)
    {
        ESP_LOGE(TAG, "JSON parse error: %s", e.what());
        return CfgError::BadJson;
    }

    if (!j.is_object())
    {
        ESP_LOGE(TAG, "JSON is not an object");
        return CfgError::BadJson;
    }

    // Step 8 — populate config fields
    auto readString = [&](const char *key, std::string &dst)
    {
        if (j.contains(key) && j[key].is_string())
            dst = j[key].get<std::string>();
    };

    readString("network_interface", cfg.networkInterface);
    readString("ip_mode", cfg.ipMode);
    readString("static_ip", cfg.staticIp);
    readString("static_mask", cfg.staticMask);
    readString("static_gateway", cfg.staticGateway);
    readString("static_dns", cfg.staticDns);
    readString("wifi_ssid", cfg.wifiSsid);
    readString("wifi_password", cfg.wifiPassword);
    readString("server_host", cfg.serverHost);

    if (j.contains("server_port") && j["server_port"].is_number_unsigned())
    {
        auto port = j["server_port"].get<unsigned>();
        if (port > 0 && port <= 65535)
            cfg.serverPort = static_cast<uint16_t>(port);
    }

    if (j.contains("server_ssl") && j["server_ssl"].is_boolean())
        cfg.serverSsl = j["server_ssl"].get<bool>();

    cfg.hasServerHost = !cfg.serverHost.empty();

    ESP_LOGI(TAG, "Parsed config: network_interface=%s ip_mode=%s", cfg.networkInterface.c_str(), cfg.ipMode.c_str());
    ESP_LOGI(TAG, "  static_ip=%s static_mask=%s static_gw=%s static_dns=%s",
             cfg.staticIp.c_str(), cfg.staticMask.c_str(),
             cfg.staticGateway.c_str(), cfg.staticDns.c_str());
    ESP_LOGI(TAG, "  wifi_ssid=%s wifi_password=%s",
             cfg.wifiSsid.c_str(),
             cfg.wifiPassword.empty() ? "(empty)" : "***");
    ESP_LOGI(TAG, "  server_host=%s server_port=%u server_ssl=%s hasServerHost=%s",
             cfg.serverHost.c_str(), cfg.serverPort,
             cfg.serverSsl ? "true" : "false",
             cfg.hasServerHost ? "true" : "false");

    return CfgError::Ok;
}

// ============================================================================
// Serialize: inverse of calaosConfigParse — same keys, same header layout
// ============================================================================

bool calaosConfigSerialize(const CalaosConfig &cfg, std::vector<uint8_t> &out)
{
    nlohmann::json j;
    j["network_interface"] = cfg.networkInterface;
    j["ip_mode"] = cfg.ipMode;
    j["static_ip"] = cfg.staticIp;
    j["static_mask"] = cfg.staticMask;
    j["static_gateway"] = cfg.staticGateway;
    j["static_dns"] = cfg.staticDns;
    j["wifi_ssid"] = cfg.wifiSsid;
    j["wifi_password"] = cfg.wifiPassword;
    j["server_host"] = cfg.serverHost;
    // Stored as unsigned so calaosConfigParse's is_number_unsigned() check passes
    j["server_port"] = static_cast<unsigned>(cfg.serverPort);
    j["server_ssl"] = cfg.serverSsl;

    std::string json = j.dump();
    if (json.empty() || json.size() > 0xFFFF)
    {
        ESP_LOGE(TAG, "Cannot serialize config: JSON payload size %zu out of range", json.size());
        return false;
    }

    const uint16_t jsonLen = static_cast<uint16_t>(json.size());
    const uint32_t crc = calaosConfigCrc32(reinterpret_cast<const uint8_t *>(json.data()), jsonLen);

    out.clear();
    out.reserve(CFG_HEADER_SIZE + jsonLen);

    // Header: magic:4 + version:1 + flags:1 + json_len:2 (LE) + crc:4 (LE)
    out.push_back(CFG_MAGIC_0);
    out.push_back(CFG_MAGIC_1);
    out.push_back(CFG_MAGIC_2);
    out.push_back(CFG_MAGIC_3);
    out.push_back(CFG_FORMAT_VERSION);
    out.push_back(0x00); // flags (reserved)
    out.push_back(static_cast<uint8_t>(jsonLen & 0xFF));
    out.push_back(static_cast<uint8_t>((jsonLen >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));

    out.insert(out.end(), json.begin(), json.end());

    ESP_LOGI(TAG, "Serialized config: %zu bytes (json=%u, crc=0x%08X)",
             out.size(), jsonLen, crc);

    return true;
}

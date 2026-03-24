#include "calaos_config.h"
#include <nlohmann/json.hpp>
#include <vector>

// ============================================================================
// CRC-32 (ISO 3309 / zlib compatible)
// ============================================================================

static const uint32_t crc32Table[256] =
{
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A53C, 0x9E6495A8, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91B, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBB, 0xE7B82D09, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F6B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0D6B, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C58, 0xDD0D7822, 0x3B6E20C8, 0x4C69105E,
    0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75,
    0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F6B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808,
    0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F,
    0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0D6B, 0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162,
    0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49,
    0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC,
    0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C58, 0xDD0D7822,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F6, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6B70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706FF,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t calaosConfigCrc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
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
        return CfgError::ReadError;

    // Step 2 — magic bytes
    if (header[0] != CFG_MAGIC_0 || header[1] != CFG_MAGIC_1 ||
        header[2] != CFG_MAGIC_2 || header[3] != CFG_MAGIC_3)
        return CfgError::NoMagic;

    // Step 3 — format version
    if (header[4] != CFG_FORMAT_VERSION)
        return CfgError::BadVersion;

    // header[5] = flags (reserved, ignored)

    // Step 4 — extract JSON payload length and expected CRC
    uint16_t jsonLen = static_cast<uint16_t>(header[6] | (header[7] << 8));
    uint32_t expectedCrc = static_cast<uint32_t>(
        header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24));

    if (jsonLen == 0)
        return CfgError::BadJson;

    // Step 5 — read JSON payload
    std::vector<uint8_t> jsonBuf(jsonLen);
    if (!reader.read(jsonBuf.data(), jsonLen))
        return CfgError::ReadError;

    // Step 6 — verify CRC
    uint32_t actualCrc = calaosConfigCrc32(jsonBuf.data(), jsonLen);
    if (actualCrc != expectedCrc)
        return CfgError::BadCrc;

    // Step 7 — parse JSON with nlohmann/json
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(jsonBuf.begin(), jsonBuf.end());
    }
    catch (const nlohmann::json::parse_error &)
    {
        return CfgError::BadJson;
    }

    if (!j.is_object())
        return CfgError::BadJson;

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

    return CfgError::Ok;
}

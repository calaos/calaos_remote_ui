/**
 * @file ota_download.h
 * @brief Firmware download loop shared by the OTA backends
 *
 * The backend supplies the HTTP stream and the partition writer; this loop
 * decides when the image may be made bootable. Anything short of a complete,
 * checksum-verified body aborts the partition, so a half-written image is
 * never selected for the next boot.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class OtaSource
{
public:
    // read() result when no byte arrived within the transport timeout.
    static constexpr int kTimeout = -2;

    virtual ~OtaSource() = default;
    virtual bool open(std::string& error) = 0;
    virtual int statusCode() = 0;
    // -1 when the server did not announce a length.
    virtual int64_t contentLength() = 0;
    // > 0: bytes read, 0: end of body, kTimeout, or any other negative: error.
    virtual int read(char* buffer, int size) = 0;
};

class OtaSink
{
public:
    virtual ~OtaSink() = default;
    virtual bool begin(int64_t imageSize, std::string& error) = 0;
    virtual bool write(const char* data, size_t size, std::string& error) = 0;
    // Validates the image and selects it for the next boot.
    virtual bool commit(std::string& error) = 0;
    virtual void abort() = 0;
};

// Gives up on a download that brings no byte for stallMs, whatever the
// per-read timeout of the transport is.
class OtaStallWatchdog
{
public:
    explicit OtaStallWatchdog(uint64_t stallMs): stallMs_(stallMs) {}
    void feed(uint64_t nowMs) { lastProgressMs_ = nowMs; }
    bool stalled(uint64_t nowMs) const { return nowMs - lastProgressMs_ >= stallMs_; }

private:
    uint64_t stallMs_;
    uint64_t lastProgressMs_ = 0;
};

struct OtaDownloadOptions
{
    std::string expectedSha256;   // lowercase hex, empty to skip
    uint64_t stallTimeoutMs = 20000;
    std::function<uint64_t()> nowMs;
    std::function<void(size_t received, size_t total)> onProgress;
    // Called once the body is complete and verified, before commit.
    std::function<void()> onVerified;
    const std::atomic<bool>* abortRequested = nullptr;
    // Caller-owned read buffer (the ESP32 wants it in internal DMA memory); allocated here when null.
    char* buffer = nullptr;
    size_t bufferSize = 4096;
};

struct OtaDownloadResult
{
    bool ok = false;
    std::string error;
    size_t received = 0;
};

OtaDownloadResult runOtaDownload(OtaSource& source, OtaSink& sink, const OtaDownloadOptions& options);

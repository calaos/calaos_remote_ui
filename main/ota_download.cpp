/**
 * @file ota_download.cpp
 */

#include "ota_download.h"
#include "mbedtls/md.h"
#include <cstdio>
#include <vector>

namespace
{
std::string toHex(const unsigned char* data, size_t len)
{
    std::string out;
    char byte[3];
    for (size_t i = 0; i < len; i++)
    {
        snprintf(byte, sizeof(byte), "%02x", data[i]);
        out += byte;
    }
    return out;
}

OtaDownloadResult failed(OtaSink* sink, const std::string& error, size_t received)
{
    if (sink)
        sink->abort();
    OtaDownloadResult r;
    r.error = error;
    r.received = received;
    return r;
}
}

OtaDownloadResult runOtaDownload(OtaSource& source, OtaSink& sink, const OtaDownloadOptions& options)
{
    std::string error;
    if (!source.open(error))
        return failed(nullptr, "Connection failed: " + error, 0);

    const int status = source.statusCode();
    if (status != 200)
        return failed(nullptr, "HTTP error: " + std::to_string(status), 0);

    const int64_t contentLength = source.contentLength();
    if (!sink.begin(contentLength, error))
        return failed(nullptr, "OTA begin failed: " + error, 0);

    // The md API is the same on the host mbedtls 2.x and the IDF mbedtls 3.x
    mbedtls_md_context_t sha;
    mbedtls_md_init(&sha);
    if (mbedtls_md_setup(&sha, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
        mbedtls_md_starts(&sha) != 0)
    {
        mbedtls_md_free(&sha);
        return failed(&sink, "SHA-256 unavailable", 0);
    }

    OtaStallWatchdog watchdog(options.stallTimeoutMs);
    watchdog.feed(options.nowMs());

    std::vector<char> owned(options.buffer ? 0 : options.bufferSize);
    char* buffer = options.buffer ? options.buffer : owned.data();
    const int bufferSize = static_cast<int>(options.bufferSize);
    size_t received = 0;
    std::string failure;

    while (failure.empty())
    {
        if (options.abortRequested && options.abortRequested->load())
        {
            failure = "Update aborted";
            break;
        }

        const int n = source.read(buffer, bufferSize);
        if (n == 0)
            break;
        if (n == OtaSource::kTimeout)
        {
            if (watchdog.stalled(options.nowMs()))
                failure = "Download stalled";
            continue;
        }
        if (n < 0)
        {
            failure = "Connection lost";
            break;
        }

        received += static_cast<size_t>(n);
        if (contentLength >= 0 && received > static_cast<size_t>(contentLength))
        {
            failure = "Server sent more than announced";
            break;
        }
        watchdog.feed(options.nowMs());
        mbedtls_md_update(&sha, reinterpret_cast<const unsigned char*>(buffer), n);
        if (!sink.write(buffer, static_cast<size_t>(n), error))
        {
            failure = "OTA write failed: " + error;
            break;
        }
        if (options.onProgress)
            options.onProgress(received, contentLength > 0 ? static_cast<size_t>(contentLength) : 0);
    }

    unsigned char digest[32];
    mbedtls_md_finish(&sha, digest);
    mbedtls_md_free(&sha);

    if (!failure.empty())
        return failed(&sink, failure, received);

    // An early end of body is how a server that stops mid-transfer looks.
    if (contentLength >= 0 && received != static_cast<size_t>(contentLength))
        return failed(&sink, "Download incomplete", received);
    if (received == 0)
        return failed(&sink, "Empty firmware", received);

    if (!options.expectedSha256.empty() && toHex(digest, sizeof(digest)) != options.expectedSha256)
        return failed(&sink, "Checksum mismatch", received);

    if (options.onVerified)
        options.onVerified();

    if (!sink.commit(error))
    {
        OtaDownloadResult r;
        r.error = "Image rejected: " + error;
        r.received = received;
        return r;
    }

    OtaDownloadResult r;
    r.ok = true;
    r.received = received;
    return r;
}

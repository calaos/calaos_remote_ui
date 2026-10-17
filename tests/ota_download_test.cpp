#include <gtest/gtest.h>
#include "ota_download.h"
#include "mbedtls/md.h"
#include <cstring>
#include <deque>

namespace
{
// One read() result per step: bytes, or kTimeout / 0 / an error code.
struct Step
{
    int result;
    std::string bytes;
};

class ScriptedSource : public OtaSource
{
public:
    bool opens = true;
    int status = 200;
    int64_t length = -1;
    std::deque<Step> steps;
    uint64_t* clock = nullptr;
    uint64_t msPerTimeout = 5000;

    bool open(std::string& error) override
    {
        if (!opens)
            error = "ECONNREFUSED";
        return opens;
    }
    int statusCode() override { return status; }
    int64_t contentLength() override { return length; }
    int read(char* buffer, int size) override
    {
        if (steps.empty())
            return 0;
        Step s = steps.front();
        steps.pop_front();
        if (s.result == kTimeout && clock)
            *clock += msPerTimeout;
        if (s.result <= 0)
            return s.result;
        int n = std::min<int>(size, s.bytes.size());
        memcpy(buffer, s.bytes.data(), n);
        return n;
    }
};

class RecordingSink : public OtaSink
{
public:
    bool begun = false, committed = false, aborted = false;
    bool rejectCommit = false;
    std::string written;

    bool begin(int64_t, std::string&) override { begun = true; return true; }
    bool write(const char* d, size_t n, std::string&) override { written.append(d, n); return true; }
    bool commit(std::string& error) override
    {
        if (rejectCommit)
        {
            error = "ESP_ERR_OTA_VALIDATE_FAILED";
            return false;
        }
        committed = true;
        return true;
    }
    void abort() override { aborted = true; }
};

std::string sha256Hex(const std::string& data)
{
    unsigned char d[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               reinterpret_cast<const unsigned char*>(data.data()), data.size(), d);
    std::string out;
    char b[3];
    for (unsigned char c : d)
    {
        snprintf(b, sizeof(b), "%02x", c);
        out += b;
    }
    return out;
}

const std::string kImage = std::string(3000, 'a') + std::string(3000, 'b');

struct Download
{
    ScriptedSource source;
    RecordingSink sink;
    uint64_t now = 0;
    OtaDownloadOptions options;

    Download()
    {
        source.clock = &now;
        source.length = kImage.size();
        options.expectedSha256 = sha256Hex(kImage);
        options.nowMs = [this]() { return now; };
    }
    OtaDownloadResult go() { return runOtaDownload(source, sink, options); }
};
}

constexpr int T = OtaSource::kTimeout;

TEST(OtaDownload, ACompleteVerifiedImageIsCommitted)
{
    Download r;
    r.source.steps = {{1, kImage.substr(0, 4000)}, {T, ""}, {1, kImage.substr(4000)}, {0, ""}};
    auto res = r.go();
    EXPECT_TRUE(res.ok) << res.error;
    EXPECT_TRUE(r.sink.committed);
    EXPECT_FALSE(r.sink.aborted);
    EXPECT_EQ(r.sink.written, kImage);
    EXPECT_EQ(res.received, kImage.size());
}

// The server process exits: the body ends early and cleanly (FIN)
TEST(OtaDownload, ABodyCutShortIsNeverCommitted)
{
    Download r;
    r.options.expectedSha256.clear();
    r.source.steps = {{1, kImage.substr(0, 4000)}, {0, ""}};
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Download incomplete");
    EXPECT_FALSE(res.permanent);
    EXPECT_TRUE(r.sink.aborted);
    EXPECT_FALSE(r.sink.committed);
}

TEST(OtaDownload, AConnectionResetIsNeverCommitted)
{
    Download r;
    r.source.steps = {{1, kImage.substr(0, 4000)}, {-1, ""}};
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Connection lost");
    EXPECT_FALSE(res.permanent);
    EXPECT_TRUE(r.sink.aborted);
    EXPECT_FALSE(r.sink.committed);
}

TEST(OtaDownload, AStalledServerIsGivenUpOnAfterTheStallTimeout)
{
    Download r;
    r.options.stallTimeoutMs = 20000;
    r.source.steps = {{1, kImage.substr(0, 4000)}};
    for (int i = 0; i < 100; i++)
        r.source.steps.push_back({T, ""});
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Download stalled");
    EXPECT_FALSE(res.permanent);
    EXPECT_TRUE(r.sink.aborted);
    EXPECT_FALSE(r.sink.committed);
    // 5 s per silent read: gives up on the 4th, not before and not much later
    EXPECT_EQ(r.now, 20000u);
}

TEST(OtaDownload, ShortSilencesDoNotTripTheWatchdog)
{
    Download r;
    r.options.stallTimeoutMs = 20000;
    r.source.steps = {{1, kImage.substr(0, 2000)}, {T, ""}, {T, ""}, {T, ""},
                      {1, kImage.substr(2000, 2000)}, {T, ""}, {T, ""}, {T, ""},
                      {1, kImage.substr(4000)}, {0, ""}};
    auto res = r.go();
    EXPECT_TRUE(res.ok) << res.error;
}

TEST(OtaDownload, AChecksumMismatchIsNeverCommitted)
{
    Download r;
    std::string bad = kImage;
    bad[10] = 'z';
    r.source.steps = {{1, bad.substr(0, 4096)}, {1, bad.substr(4096)}, {0, ""}};
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Checksum mismatch");
    EXPECT_TRUE(res.permanent);
    EXPECT_TRUE(r.sink.aborted);
    EXPECT_FALSE(r.sink.committed);
}

TEST(OtaDownload, MoreBytesThanAnnouncedAreRefused)
{
    Download r;
    r.source.length = 100;
    r.options.expectedSha256.clear();
    r.source.steps = {{1, kImage.substr(0, 101)}, {0, ""}};
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.permanent);
    EXPECT_TRUE(r.sink.aborted);
}

TEST(OtaDownload, WithoutALengthTheChecksumStillDecides)
{
    Download r;
    r.source.length = -1;
    r.source.steps = {{1, kImage.substr(0, 4096)}, {1, kImage.substr(4096)}, {0, ""}};
    EXPECT_TRUE(r.go().ok);

    Download e;
    e.source.length = -1;
    e.options.expectedSha256.clear();
    e.source.steps = {{0, ""}};
    auto res = e.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Empty firmware");
}

TEST(OtaDownload, AnAbsentServerFailsBeforeThePartitionIsTouched)
{
    Download r;
    r.source.opens = false;
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.error, "Connection failed: ECONNREFUSED");
    EXPECT_FALSE(res.permanent);
    EXPECT_FALSE(r.sink.begun);

    Download s;
    s.source.status = 503;
    res = s.go();
    EXPECT_EQ(res.error, "HTTP error: 503");
    EXPECT_FALSE(res.permanent);
    EXPECT_FALSE(s.sink.begun);

    Download nf;
    nf.source.status = 404;
    res = nf.go();
    EXPECT_TRUE(res.permanent);
}

TEST(OtaDownload, AnImageTheBootloaderRejectsIsAnError)
{
    Download r;
    r.sink.rejectCommit = true;
    r.source.steps = {{1, kImage.substr(0, 4096)}, {1, kImage.substr(4096)}, {0, ""}};
    bool verified = false;
    r.options.onVerified = [&verified]() { verified = true; };
    auto res = r.go();
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(verified);
    EXPECT_EQ(res.error, "Image rejected: ESP_ERR_OTA_VALIDATE_FAILED");
    EXPECT_TRUE(res.permanent);
}

TEST(OtaDownload, AnAbortStopsTheDownload)
{
    Download r;
    std::atomic<bool> abort{false};
    r.options.abortRequested = &abort;
    int calls = 0;
    r.options.onProgress = [&](size_t, size_t total)
    {
        EXPECT_EQ(total, kImage.size());
        if (++calls == 1)
            abort = true;
    };
    r.source.steps = {{1, kImage.substr(0, 4096)}, {1, kImage.substr(4096)}, {0, ""}};
    auto res = r.go();
    EXPECT_EQ(res.error, "Update aborted");
    EXPECT_TRUE(r.sink.aborted);
    EXPECT_FALSE(r.sink.committed);
}

TEST(OtaDownload, ACallerBufferIsUsed)
{
    Download r;
    char buf[1000];
    r.options.buffer = buf;
    r.options.bufferSize = sizeof(buf);
    r.source.steps = {{1, kImage.substr(0, 1000)}, {1, kImage.substr(1000, 1000)}, {1, kImage.substr(2000, 1000)},
                      {1, kImage.substr(3000, 1000)}, {1, kImage.substr(4000, 1000)}, {1, kImage.substr(5000)}, {0, ""}};
    EXPECT_TRUE(r.go().ok);
}

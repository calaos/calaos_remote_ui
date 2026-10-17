#include "fake_ota.h"
#include <memory>

namespace
{
FakeOta* g_instance = nullptr;
}

FakeOta* FakeOta::instance()
{
    return g_instance;
}

HalResult FakeOta::startUpdate(const std::string& url,
                               const std::string&,
                               const std::map<std::string, std::string>&,
                               HalOtaProgressCallback progressCallback)
{
    if (refuseStart.load())
        return HalResult::ERROR;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = progressCallback;
        lastUrl_ = url;
    }
    starts_++;
    return HalResult::OK;
}

void FakeOta::report(HalOtaState state, const std::string& error)
{
    HalOtaProgressCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = callback_;
    }
    HalOtaProgress p;
    p.state = state;
    p.errorMessage = error;
    if (cb)
        cb(p);
}

std::string FakeOta::lastUrl() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastUrl_;
}

std::unique_ptr<HalOta> createOta()
{
    auto ota = std::make_unique<FakeOta>();
    g_instance = ota.get();
    return ota;
}

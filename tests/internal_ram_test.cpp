#include <gtest/gtest.h>
#include "external_ram.h"
#include "support/prod_config.h"
#include "support/ws_harness.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>

// Own executable: replacing operator new here must not take the sanitizers'
// new/delete checks away from the other tests.
namespace
{
struct alignas(alignof(std::max_align_t)) Header
{
    std::size_t size;
    bool internal;
};

thread_local bool measuredThread = false;
std::atomic<long> internalLive{0};
std::atomic<long> internalPeak{0};
std::atomic<long> externalTotal{0};

void raisePeak(long live)
{
    long peak = internalPeak.load();
    while (live > peak && !internalPeak.compare_exchange_weak(peak, live))
    {
    }
}

// Live bytes allocated by the measured thread outside an ExternalRamScope:
// on the target, the ones malloc serves from internal RAM first.
struct InternalRamMeter
{
    InternalRamMeter()
    {
        internalLive = 0;
        internalPeak = 0;
        externalTotal = 0;
        measuredThread = true;
    }

    ~InternalRamMeter()
    {
        measuredThread = false;
    }
};
}

void* operator new(std::size_t size)
{
    auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!h)
        throw std::bad_alloc();
    h->size = size;
    h->internal = false;
    if (measuredThread)
    {
        if (ExternalRamScope::activeOnThisThread())
        {
            externalTotal += static_cast<long>(size);
        }
        else
        {
            h->internal = true;
            raisePeak(internalLive += static_cast<long>(size));
        }
    }
    return h + 1;
}

void operator delete(void* p) noexcept
{
    if (!p)
        return;
    auto* h = static_cast<Header*>(p) - 1;
    if (h->internal)
        internalLive -= static_cast<long>(h->size);
    std::free(h);
}

void operator delete(void* p, std::size_t) noexcept
{
    operator delete(p);
}

// Under ASan the default array and nothrow forms bypass the replaced operator
// new, so every form must be replaced or a delete reads a header that is not there.
void* operator new[](std::size_t size)
{
    return operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}

void operator delete[](void* p) noexcept
{
    operator delete(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    operator delete(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    operator delete(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    operator delete(p);
}

TEST(ExternalRamScope, ActiveOnlyOnItsThreadAndUntilItEnds)
{
    EXPECT_FALSE(ExternalRamScope::activeOnThisThread());
    {
        ExternalRamScope outer;
        EXPECT_TRUE(ExternalRamScope::activeOnThisThread());
        {
            ExternalRamScope nested;
            EXPECT_TRUE(ExternalRamScope::activeOnThisThread());
        }
        EXPECT_TRUE(ExternalRamScope::activeOnThisThread());

        bool otherSees = true;
        bool otherOwnSees = false;
        std::thread([&]
        {
            otherSees = ExternalRamScope::activeOnThisThread();
            ExternalRamScope contended;
            otherOwnSees = ExternalRamScope::activeOnThisThread();
        }).join();
        EXPECT_FALSE(otherSees);
        EXPECT_FALSE(otherOwnSees);
    }
    EXPECT_FALSE(ExternalRamScope::activeOnThisThread());

    std::thread([]
    {
        ExternalRamScope other;
        EXPECT_TRUE(ExternalRamScope::activeOnThisThread());
    }).join();
    EXPECT_FALSE(ExternalRamScope::activeOnThisThread());
}

// The DOM of a big config_update is the allocation peak that drained internal
// RAM on the panel: all of it, and what the handler builds from it, must be
// allocated under the scope.
TEST(InternalRam, BigConfigUpdateAllocatesNothingOutsideTheExternalRamScope)
{
    EventProbe& probe = EventProbe::instance();
    CalaosWebSocketManager mgr;
    const WebSocketMessage msg(
        nlohmann::json{{"msg", CalaosProtocol::MSG_CONFIG_UPDATE}, {"data", prodSizedConfig()}}.dump());

    long internal = 0;
    long external = 0;
    {
        InternalRamMeter meter;
        CalaosWebSocketManagerTestAccess::onMessage(mgr, msg);
        internal = internalPeak;
        external = externalTotal;
    }
    auto events = probe.settle();

    ASSERT_NE(firstData<ConfigUpdateReceivedData>(events, AppEventType::ConfigUpdateReceived), nullptr);
    RecordProperty("message_bytes", static_cast<int>(msg.data.size()));
    RecordProperty("internal_peak_bytes", static_cast<int>(internal));
    RecordProperty("external_bytes", static_cast<int>(external));
    EXPECT_GT(external, static_cast<long>(msg.data.size()));
    EXPECT_EQ(internal, 0);
}

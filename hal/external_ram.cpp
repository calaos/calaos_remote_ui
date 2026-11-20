#include "external_ram.h"

#include <atomic>
#include <cstdlib>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace
{
// One slot per task that may hold a scope (ws_service, app_dispatcher, spare).
// A fixed table: operator new reads it, so it cannot allocate.
constexpr int kMaxScopes = 4;
std::atomic<void*> scopeOwners[kMaxScopes];

// No C++ thread_local on the target: operator new runs in global constructors,
// before the scheduler gives tasks their thread pointer.
void* currentThreadToken()
{
#ifdef ESP_PLATFORM
    return xTaskGetCurrentTaskHandle();
#else
    static thread_local char token;
    return &token;
#endif
}
}

ExternalRamScope::ExternalRamScope()
{
    void* token = currentThreadToken();
    // Null before the scheduler starts: it would match a free slot.
    if (!token || activeOnThisThread())
        return;
    for (int i = 0; i < kMaxScopes; i++)
    {
        void* expected = nullptr;
        if (scopeOwners[i].compare_exchange_strong(expected, token))
        {
            slot_ = i;
            return;
        }
    }
}

ExternalRamScope::~ExternalRamScope()
{
    if (slot_ >= 0)
        scopeOwners[slot_].store(nullptr);
}

bool ExternalRamScope::activeOnThisThread()
{
    void* token = currentThreadToken();
    if (!token)
        return false;
    for (int i = 0; i < kMaxScopes; i++)
        if (scopeOwners[i].load(std::memory_order_relaxed) == token)
            return true;
    return false;
}

#ifdef ESP_PLATFORM

// Replaces the libstdc++ operator new, which only calls malloc. Its new[] form
// calls this one; the nothrow form is replaced below so that it cannot silently
// go back to malloc. delete stays free(), which takes both RAMs.
// Throwing is what the standard requires of this function.
void* operator new(std::size_t size)
{
    if (size == 0)
        size = 1;
    for (;;)
    {
        void* p = ExternalRamScope::activeOnThisThread()
                      ? heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT)
                      : malloc(size);
        if (p)
            return p;
        std::new_handler handler = std::get_new_handler();
        if (!handler)
            throw std::bad_alloc();
        handler();
    }
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

static const char* TAG = "heap";

namespace
{
std::atomic<bool> watchRunning{false};
std::atomic<unsigned> beginStackLeft{0};
}

void InternalHeapWatch::begin()
{
    if (watchRunning.exchange(false))
        heap_caps_monitor_local_minimum_free_size_stop();
    if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK)
        return;
    beginStackLeft = uxTaskGetStackHighWaterMark(nullptr);
    watchRunning = true;
}

void InternalHeapWatch::end(const char* operation)
{
    if (!watchRunning.exchange(false))
        return;

    // Every internal heap carries MALLOC_CAP_DEFAULT here, the reserve pool
    // included: malloc and FreeRTOS draw on the same total, the pool last.
    size_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
#ifdef CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL
    const size_t reserve = CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL;
#else
    const size_t reserve = 0;
#endif

    ESP_LOGI(TAG, "%s: internal low=%u free=%u largest=%u reserve=%u, psram free=%u, stack left=%u/%u",
             operation, (unsigned)low,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)reserve,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)beginStackLeft.load(),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    // Per heap, "minimum" is the low of this operation while monitoring runs:
    // the reserve pool is the internal heap of its size.
    if (low < 2 * reserve)
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    heap_caps_monitor_local_minimum_free_size_stop();
}

#else

void InternalHeapWatch::begin() {}

void InternalHeapWatch::end(const char*) {}

#endif

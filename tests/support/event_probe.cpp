#include "event_probe.h"
#include "app_dispatcher.h"
#include "app_store.h"
#include <chrono>
#include <stdexcept>

namespace
{
// Nothing under test emits this event, so it cannot be confused with real traffic.
constexpr AppEventType kSentinel = AppEventType::ScreenSaverDeactivated;
}

EventProbe& EventProbe::instance()
{
    static EventProbe probe;
    return probe;
}

EventProbe::EventProbe()
{
    // AppStore subscribes in its constructor; creating it first keeps it ahead of
    // the probe in the subscriber list, so it has handled an event before the
    // probe reports it.
    AppStore::getInstance();

    AppDispatcher::getInstance().subscribe([this](const AppEvent& event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (event.getType() == kSentinel)
            sentinelSeen_ = true;
        else
            events_.push_back(event);
        cv_.notify_all();
    });
}

std::vector<AppEvent> EventProbe::settle()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sentinelSeen_ = false;
    }

    AppDispatcher::getInstance().dispatch(AppEvent(kSentinel));

    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(5), [this] { return sentinelSeen_; }))
        throw std::runtime_error("AppDispatcher did not drain within 5s");

    std::vector<AppEvent> out;
    out.swap(events_);
    return out;
}

#pragma once

#include "app_event.h"
#include <condition_variable>
#include <mutex>
#include <vector>

// Records what the real AppDispatcher worker thread delivers. The dispatcher is
// asynchronous, so settle() pushes a sentinel through the same FIFO queue and
// waits for it: once it is seen, every earlier event has been handled by all
// subscribers, including AppStore.
class EventProbe
{
public:
    static EventProbe& instance();

    std::vector<AppEvent> settle();

private:
    EventProbe();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<AppEvent> events_;
    bool sentinelSeen_ = false;
};

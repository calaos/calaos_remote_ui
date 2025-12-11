#pragma once

#include "app_event.h"
#include "thread_safety.h"
#include <functional>
#include <vector>
#include <memory>
#include <queue>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
#else
#include <thread>
#include <condition_variable>
#include <atomic>
#endif

using AppEventCallback = std::function<void(const AppEvent&)>;

class AppDispatcher
{
public:
    static AppDispatcher& getInstance();
    ~AppDispatcher();

    // Register a callback for all events
    void subscribe(AppEventCallback callback);

    // Register a callback for specific event type
    void subscribe(AppEventType eventType, AppEventCallback callback);

    // Dispatch an event to all registered callbacks (non-blocking)
    void dispatch(const AppEvent& event);

    // Clear all subscribers (useful for cleanup)
    void clearSubscribers();

    // Check if the dispatcher is stopping (useful to avoid deadlocks during shutdown)
    bool isStopping() const { return shouldStop_.load(); }

    // Explicitly stop the worker thread (call before application cleanup)
    void shutdown();

private:
    AppDispatcher();

    // Worker thread function
    void processEvents();

    // Platform-specific worker thread implementations
#ifdef ESP_PLATFORM
    static void workerTaskFunction(void* parameter);
    void startWorkerTask();
    void stopWorkerTask();
#else
    void startWorkerThread();
    void stopWorkerThread();
#endif

    struct Subscription
    {
        AppEventType eventType;
        AppEventCallback callback;
        bool listenAllEvents;

        Subscription(AppEventCallback cb) : callback(cb), listenAllEvents(true) {}
        Subscription(AppEventType type, AppEventCallback cb) : eventType(type), callback(cb), listenAllEvents(false) {}
    };

    // Subscribers management
    std::vector<Subscription> subscribers_;
    flux::Mutex subscribersMutex_;

    // Event queue and worker thread management
#ifdef ESP_PLATFORM
    // FreeRTOS implementation - use pointers to avoid copy issues with complex objects
    QueueHandle_t eventQueue_;
    TaskHandle_t workerTaskHandle_;
    std::atomic<bool> shouldStop_;
    static const int QUEUE_SIZE = 32;
    static const int TASK_STACK_SIZE = 8192;
    static const int TASK_PRIORITY = 5;
#else
    // Linux/std implementation
    std::queue<AppEvent> eventQueue_;
    flux::Mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::thread workerThread_;
    std::atomic<bool> shouldStop_;
#endif
};
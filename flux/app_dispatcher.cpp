#include "app_dispatcher.h"
#include "logging.h"

#ifdef ESP_PLATFORM
#include <atomic>
#else
#include <atomic>
#endif

static const char* TAG = "AppDispatcher";

AppDispatcher& AppDispatcher::getInstance()
{
    static AppDispatcher instance;
    return instance;
}

AppDispatcher::AppDispatcher() : shouldStop_(false)
{
#ifdef ESP_PLATFORM
    startWorkerTask();
#else
    startWorkerThread();
#endif
}

AppDispatcher::~AppDispatcher()
{
#ifdef ESP_PLATFORM
    stopWorkerTask();
#else
    stopWorkerThread();
#endif
}

void AppDispatcher::subscribe(AppEventCallback callback)
{
    flux::LockGuard lock(subscribersMutex_);
    subscribers_.emplace_back(callback);
}

void AppDispatcher::subscribe(AppEventType eventType, AppEventCallback callback)
{
    flux::LockGuard lock(subscribersMutex_);
    subscribers_.emplace_back(eventType, callback);
}

void AppDispatcher::dispatch(const AppEvent& event)
{
#ifdef ESP_PLATFORM
    // Create a copy of the event on the heap to avoid copy issues
    AppEvent* eventPtr = new AppEvent(event);
    if (eventQueue_ && xQueueSend(eventQueue_, &eventPtr, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Event queue full, dropping event type %d", static_cast<int>(event.getType()));
        delete eventPtr;  // Clean up if queue is full
    }
#else
    // Add event to std::queue (non-blocking)
    {
        flux::LockGuard lock(queueMutex_);
        eventQueue_.push(event);
    }
    queueCondition_.notify_one();
#endif
}

void AppDispatcher::clearSubscribers()
{
    flux::LockGuard lock(subscribersMutex_);
    subscribers_.clear();
}

void AppDispatcher::processEvents()
{
    while (!shouldStop_.load())
    {
        bool hasEvent = false;

#ifdef ESP_PLATFORM
        // FreeRTOS: Wait for event pointer from queue
        AppEvent* eventPtr = nullptr;
        if (xQueueReceive(eventQueue_, &eventPtr, pdMS_TO_TICKS(100)) == pdTRUE && eventPtr)
        {
            hasEvent = true;
        }
#else
        AppEvent event(AppEventType::NetworkStatusChanged); // Default initialization
        // Linux: Wait for event from condition variable
        {
            std::unique_lock<flux::Mutex> lock(queueMutex_);
            queueCondition_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !eventQueue_.empty() || shouldStop_.load();
            });

            if (!eventQueue_.empty())
            {
                event = eventQueue_.front();
                eventQueue_.pop();
                hasEvent = true;
            }
        }
#endif

        if (hasEvent)
        {
#ifdef ESP_PLATFORM
            // Process the event by calling all matching subscribers (ESP32 version)
            flux::LockGuard lock(subscribersMutex_);
            for (const auto& subscription : subscribers_)
            {
                if (subscription.listenAllEvents || subscription.eventType == eventPtr->getType())
                    subscription.callback(*eventPtr);
            }
            // Clean up the dynamically allocated event
            delete eventPtr;
#else
            // Process the event by calling all matching subscribers (Linux version)
            flux::LockGuard lock(subscribersMutex_);
            for (const auto& subscription : subscribers_)
            {
                if (subscription.listenAllEvents || subscription.eventType == event.getType())
                    subscription.callback(event);
            }
#endif
        }
    }

    ESP_LOGI(TAG, "Event processing thread stopped");
}

#ifdef ESP_PLATFORM
void AppDispatcher::workerTaskFunction(void* parameter)
{
    AppDispatcher* dispatcher = static_cast<AppDispatcher*>(parameter);
    dispatcher->processEvents();
    vTaskDelete(nullptr);
}

void AppDispatcher::startWorkerTask()
{
    ESP_LOGI(TAG, "Starting worker task");

    // Create event queue for pointers
    eventQueue_ = xQueueCreate(QUEUE_SIZE, sizeof(AppEvent*));
    if (!eventQueue_)
    {
        ESP_LOGE(TAG, "Failed to create event queue");
        return;
    }

    // Create worker task
    BaseType_t result = xTaskCreate(
        workerTaskFunction,
        "app_dispatcher",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &workerTaskHandle_
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
        vQueueDelete(eventQueue_);
        eventQueue_ = nullptr;
    }
}

void AppDispatcher::stopWorkerTask()
{
    ESP_LOGI(TAG, "Stopping worker task");

    shouldStop_.store(true);

    if (workerTaskHandle_)
    {
        // Wait for task to complete (with timeout)
        eTaskState taskState;
        int timeout = 100; // 1 second timeout
        do
        {
            taskState = eTaskGetState(workerTaskHandle_);
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout--;
        } while (taskState != eDeleted && timeout > 0);

        if (timeout == 0)
        {
            ESP_LOGW(TAG, "Worker task did not stop gracefully");
        }

        workerTaskHandle_ = nullptr;
    }

    if (eventQueue_)
    {
        // Clean up any remaining events in queue to prevent memory leaks
        AppEvent* eventPtr;
        while (xQueueReceive(eventQueue_, &eventPtr, 0) == pdTRUE)
        {
            if (eventPtr)
            {
                delete eventPtr;
            }
        }
        
        vQueueDelete(eventQueue_);
        eventQueue_ = nullptr;
    }
}

#else
void AppDispatcher::startWorkerThread()
{
    ESP_LOGI(TAG, "Starting worker thread");
    workerThread_ = std::thread(&AppDispatcher::processEvents, this);
}

void AppDispatcher::stopWorkerThread()
{
    ESP_LOGI(TAG, "Stopping worker thread");

    shouldStop_.store(true);
    queueCondition_.notify_all();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
}
#endif
#include "lvgl_timer.h"
#include "logging.h"
#include "misc/lv_timer.h"
#include "misc/lv_async.h"
#include <vector>
#include <algorithm>

static const char* TAG = "LvglTimer";

LvglTimer::LvglTimer(TimerCallback callback, uint32_t period_ms)
    : timer_(nullptr), callback_(std::move(callback))
{
    if (callback_)
    {
        timer_ = lv_timer_create(timerCallbackWrapper, period_ms, this);
        if (!timer_)
        {
            ESP_LOGE(TAG, "Failed to create LVGL timer");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Timer created with null callback");
    }
}

LvglTimer::LvglTimer(TimerCallback callback, uint32_t period_ms, int32_t repeat_count)
    : LvglTimer(std::move(callback), period_ms)
{
    if (timer_)
    {
        lv_timer_set_repeat_count(timer_, repeat_count);
        // Let LVGL auto-delete one-shot timers (default behavior)
        // The C++ wrapper will be notified and can clean up safely
    }
}

LvglTimer::~LvglTimer()
{
    cleanup();
}

LvglTimer::LvglTimer(LvglTimer&& other) noexcept
    : timer_(other.timer_), callback_(std::move(other.callback_))
{
    other.timer_ = nullptr;
    other.callback_ = nullptr;

    // Update user_data to point to new object
    if (timer_)
    {
        lv_timer_set_user_data(timer_, this);
    }
}

LvglTimer& LvglTimer::operator=(LvglTimer&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        timer_ = other.timer_;
        callback_ = std::move(other.callback_);

        other.timer_ = nullptr;
        other.callback_ = nullptr;

        // Update user_data to point to new object
        if (timer_)
        {
            lv_timer_set_user_data(timer_, this);
        }
    }
    return *this;
}

void LvglTimer::start()
{
    if (timer_)
    {
        lv_timer_resume(timer_);
    }
}

void LvglTimer::pause()
{
    if (timer_)
    {
        lv_timer_pause(timer_);
    }
}

void LvglTimer::resume()
{
    if (timer_)
    {
        lv_timer_resume(timer_);
    }
}

void LvglTimer::reset()
{
    if (timer_)
    {
        lv_timer_reset(timer_);
    }
}

void LvglTimer::destroy()
{
    cleanup();
}

void LvglTimer::setPeriod(uint32_t period_ms)
{
    if (timer_)
    {
        lv_timer_set_period(timer_, period_ms);
    }
}

void LvglTimer::setRepeatCount(int32_t repeat_count)
{
    if (timer_)
    {
        lv_timer_set_repeat_count(timer_, repeat_count);
    }
}

void LvglTimer::setCallback(TimerCallback callback)
{
    callback_ = std::move(callback);
}

bool LvglTimer::isValid() const
{
    return timer_ != nullptr;
}

bool LvglTimer::isPaused() const
{
    // Note: LVGL doesn't provide a public API to check paused state
    // We'll just return false for simplicity
    return false;
}

uint32_t LvglTimer::getPeriod() const
{
    // Note: LVGL doesn't provide a public API to get period
    // We'll just return 0 for simplicity
    return 0;
}

int32_t LvglTimer::getRepeatCount() const
{
    // Note: LVGL doesn't provide a public API to get repeat count
    // We'll just return 0 for simplicity
    return 0;
}

// Static container to hold one-shot timers until they execute
static std::vector<std::unique_ptr<LvglTimer>> oneShotTimers;

// Helper structure for one-shot timers that self-delete
struct OneShotTimerData
{
    LvglTimer::TimerCallback callback;
    LvglTimer* wrapper;  // Points to the wrapper that will be deleted
};

static void oneShotCallbackWrapper(lv_timer_t* timer)
{
    if (timer)
    {
        void* user_data = lv_timer_get_user_data(timer);
        if (user_data)
        {
            OneShotTimerData* data = static_cast<OneShotTimerData*>(user_data);

            // Execute user callback
            if (data->callback)
            {
                data->callback();
            }

            // Schedule async deletion of the wrapper to avoid deleting during LVGL iteration
            LvglTimer* wrapper_to_delete = data->wrapper;
            lv_async_call([](void* param) {
                LvglTimer* wrapper = static_cast<LvglTimer*>(param);
                // Find and remove from container
                oneShotTimers.erase(
                    std::remove_if(oneShotTimers.begin(), oneShotTimers.end(),
                        [wrapper](const std::unique_ptr<LvglTimer>& t) {
                            return t.get() == wrapper;
                        }),
                    oneShotTimers.end());
            }, wrapper_to_delete);

            delete data;
        }
    }
}

std::unique_ptr<LvglTimer> LvglTimer::createOneShot(TimerCallback callback, uint32_t delay_ms)
{
    // Create a special wrapper that will be auto-deleted by LVGL
    auto data = new OneShotTimerData{std::move(callback), nullptr};

    // Create a timer with custom callback wrapper that handles self-deletion
    auto wrapper = std::make_unique<LvglTimer>([](){ /* dummy */ }, delay_ms, 1);

    if (!wrapper->isValid())
    {
        delete data;
        return nullptr;
    }

    // Store wrapper pointer in data
    data->wrapper = wrapper.get();

    // Replace the timer callback and user_data
    lv_timer_set_cb(wrapper->timer_, oneShotCallbackWrapper);
    lv_timer_set_user_data(wrapper->timer_, data);

    // Disable auto-delete since we'll delete the lv_timer_t ourselves
    lv_timer_set_auto_delete(wrapper->timer_, false);

    // Store in container
    oneShotTimers.push_back(std::move(wrapper));

    // Return nullptr since caller doesn't need to manage lifetime
    return nullptr;
}

std::unique_ptr<LvglTimer> LvglTimer::createRepeating(TimerCallback callback, uint32_t period_ms)
{
    auto timer = std::make_unique<LvglTimer>(std::move(callback), period_ms, -1);
    return timer->isValid() ? std::move(timer) : nullptr;
}

void LvglTimer::timerCallbackWrapper(lv_timer_t* timer)
{
    if (timer)
    {
        void* user_data = lv_timer_get_user_data(timer);
        if (user_data)
        {
            LvglTimer* lvglTimer = static_cast<LvglTimer*>(user_data);

            if (lvglTimer->callback_)
            {
                lvglTimer->callback_();
            }
        }
    }
}

void LvglTimer::cleanup()
{
    if (timer_)
    {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    callback_ = nullptr;
}
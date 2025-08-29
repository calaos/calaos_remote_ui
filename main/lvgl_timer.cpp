#include "lvgl_timer.h"
#include "logging.h"
#include "misc/lv_timer.h"

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

std::unique_ptr<LvglTimer> LvglTimer::createOneShot(TimerCallback callback, uint32_t delay_ms)
{
    auto timer = std::make_unique<LvglTimer>(std::move(callback), delay_ms, 1);
    return timer->isValid() ? std::move(timer) : nullptr;
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
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    callback_ = nullptr;
}
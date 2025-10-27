#pragma once

#include "lvgl.h"
#include <functional>
#include <memory>

class LvglTimer
{
public:
    using TimerCallback = std::function<void()>;

    // Constructor with callback and period in milliseconds
    LvglTimer(TimerCallback callback, uint32_t period_ms);

    // Constructor with callback, period and repeat count
    LvglTimer(TimerCallback callback, uint32_t period_ms, int32_t repeat_count);

    // Destructor - automatically cleans up timer
    ~LvglTimer();

    // Delete copy constructor and assignment operator
    LvglTimer(const LvglTimer&) = delete;
    LvglTimer& operator=(const LvglTimer&) = delete;

    // Move constructor and assignment operator
    LvglTimer(LvglTimer&& other) noexcept;
    LvglTimer& operator=(LvglTimer&& other) noexcept;

    // Timer control methods
    void start();
    void pause();
    void resume();
    void reset();
    void destroy();

    // Timer configuration methods
    void setPeriod(uint32_t period_ms);
    void setRepeatCount(int32_t repeat_count);
    void setCallback(TimerCallback callback);

    // Timer state methods
    bool isValid() const;
    bool isPaused() const;
    uint32_t getPeriod() const;
    int32_t getRepeatCount() const;

    // Static factory methods for common use cases
    static std::unique_ptr<LvglTimer> createOneShot(TimerCallback callback, uint32_t delay_ms);
    static std::unique_ptr<LvglTimer> createRepeating(TimerCallback callback, uint32_t period_ms);

private:
    static void timerCallbackWrapper(lv_timer_t* timer);
    void cleanup();

    lv_timer_t* timer_ = nullptr;
    TimerCallback callback_;
};
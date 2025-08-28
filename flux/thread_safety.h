#pragma once

#ifdef ESP_PLATFORM
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
    #include "freertos/task.h"
#else
    #include <mutex>
#endif

namespace flux
{

#ifdef ESP_PLATFORM
    // FreeRTOS-based implementation for ESP32
    class Mutex
    {
    public:
        Mutex()
        {
            mutexHandle = xSemaphoreCreateMutex();
        }
        
        ~Mutex()
        {
            if (mutexHandle)
                vSemaphoreDelete(mutexHandle);
        }
        
        void lock()
        {
            if (mutexHandle)
                xSemaphoreTake(mutexHandle, portMAX_DELAY);
        }
        
        void unlock()
        {
            if (mutexHandle)
                xSemaphoreGive(mutexHandle);
        }
        
        bool tryLock()
        {
            if (mutexHandle)
                return xSemaphoreTake(mutexHandle, 0) == pdTRUE;
            return false;
        }
        
        // Check if we're in ISR context
        static bool isInISRContext()
        {
            return xPortInIsrContext() == pdTRUE;
        }
        
    private:
        SemaphoreHandle_t mutexHandle;
        
        // Non-copyable
        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;
    };
    
    // RAII lock guard for FreeRTOS
    class LockGuard
    {
    public:
        explicit LockGuard(Mutex& m) : mutex(m)
        {
            // Don't lock if we're in ISR context - this prevents deadlocks
            if (!Mutex::isInISRContext())
            {
                mutex.lock();
                locked = true;
            }
            else
            {
                locked = false;
                // In ISR context, we should use different synchronization or defer the operation
            }
        }
        
        ~LockGuard()
        {
            if (locked)
                mutex.unlock();
        }
        
        bool isLocked() const { return locked; }
        
    private:
        Mutex& mutex;
        bool locked;
        
        // Non-copyable
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    };

#else
    // Standard C++ implementation for Linux
    using Mutex = std::mutex;
    using LockGuard = std::lock_guard<std::mutex>;
    
#endif

} // namespace flux
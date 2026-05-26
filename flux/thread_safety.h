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
    // FreeRTOS-based implementation for ESP32.
    // Recursive so a task can re-enter critical sections it already owns
    // (e.g. an AppStore callback that calls AppStore::getState()).
    class Mutex
    {
    public:
        Mutex()
        {
            mutexHandle = xSemaphoreCreateRecursiveMutex();
        }

        ~Mutex()
        {
            if (mutexHandle)
                vSemaphoreDelete(mutexHandle);
        }

        void lock()
        {
            if (mutexHandle)
                xSemaphoreTakeRecursive(mutexHandle, portMAX_DELAY);
        }

        void unlock()
        {
            if (mutexHandle)
                xSemaphoreGiveRecursive(mutexHandle);
        }

        bool tryLock()
        {
            if (mutexHandle)
                return xSemaphoreTakeRecursive(mutexHandle, 0) == pdTRUE;
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
    // Standard C++ implementation for Linux (recursive to match ESP32 side)
    using Mutex = std::recursive_mutex;
    using LockGuard = std::lock_guard<std::recursive_mutex>;
    
#endif

} // namespace flux
#pragma once

#include "app_event.h"
#include "thread_safety.h"
#include <functional>
#include <vector>
#include <memory>

using AppEventCallback = std::function<void(const AppEvent&)>;

class AppDispatcher
{
public:
    static AppDispatcher& getInstance();
    
    // Register a callback for all events
    void subscribe(AppEventCallback callback);
    
    // Register a callback for specific event type
    void subscribe(AppEventType eventType, AppEventCallback callback);
    
    // Dispatch an event to all registered callbacks
    void dispatch(const AppEvent& event);
    
    // Clear all subscribers (useful for cleanup)
    void clearSubscribers();

private:
    AppDispatcher() = default;
    
    struct Subscription
    {
        AppEventType eventType;
        AppEventCallback callback;
        bool listenAllEvents;
        
        Subscription(AppEventCallback cb) : callback(cb), listenAllEvents(true) {}
        Subscription(AppEventType type, AppEventCallback cb) : eventType(type), callback(cb), listenAllEvents(false) {}
    };
    
    std::vector<Subscription> subscribers_;
    flux::Mutex mutex_;
};
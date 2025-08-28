#include "app_dispatcher.h"

AppDispatcher& AppDispatcher::getInstance()
{
    static AppDispatcher instance;
    return instance;
}

void AppDispatcher::subscribe(AppEventCallback callback)
{
    flux::LockGuard lock(mutex_);
    subscribers_.emplace_back(callback);
}

void AppDispatcher::subscribe(AppEventType eventType, AppEventCallback callback)
{
    flux::LockGuard lock(mutex_);
    subscribers_.emplace_back(eventType, callback);
}

void AppDispatcher::dispatch(const AppEvent& event)
{
    flux::LockGuard lock(mutex_);
    
    for (const auto& subscription : subscribers_)
    {
        if (subscription.listenAllEvents || subscription.eventType == event.getType())
            subscription.callback(event);
    }
}

void AppDispatcher::clearSubscribers()
{
    flux::LockGuard lock(mutex_);
    subscribers_.clear();
}
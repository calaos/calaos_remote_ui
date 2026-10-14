#pragma once

#include "calaos_websocket_manager.h"
#include "event_probe.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct CalaosWebSocketManagerTestAccess
{
    static void onMessage(CalaosWebSocketManager& mgr, const WebSocketMessage& msg)
    {
        mgr.onMessage(msg);
    }

    // Makes mgr look connected and hands what it sends to sink
    static void connect(CalaosWebSocketManager& mgr, std::function<bool(const std::string&)> sink)
    {
        mgr.currentState_ = WebSocketState::CONNECTED;
        mgr.sendSink_ = std::move(sink);
    }
};

// Sends one server message through the same entry point as the real socket and
// returns the events that reached the dispatcher subscribers.
inline std::vector<AppEvent> receive(const std::string& rawJson)
{
    // Must exist before the message is handled, or the worker thread can deliver
    // events before the probe has subscribed.
    EventProbe& probe = EventProbe::instance();
    CalaosWebSocketManager mgr;
    CalaosWebSocketManagerTestAccess::onMessage(mgr, WebSocketMessage(rawJson));
    return probe.settle();
}

inline std::vector<AppEvent> receive(const char* msg, const nlohmann::json& data)
{
    return receive(nlohmann::json{{"msg", msg}, {"data", data}}.dump());
}

// The pointer is into `events`: keep them in a variable for as long as it is used.
template<typename T>
const T* firstData(const std::vector<AppEvent>& events, AppEventType type)
{
    for (const auto& e : events)
        if (e.getType() == type)
            return e.getData<T>();
    return nullptr;
}

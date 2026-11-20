#pragma once

// While a scope lives, operator new on its thread prefers PSRAM whatever the
// size. The DOM of a big server message is thousands of small blocks that
// malloc serves from internal RAM first, the only RAM FreeRTOS objects can use.
// Each thread holds its own scope (a few slots, then a no-op); a scope nested
// on the same thread is a no-op too.
// The host build has no PSRAM: only activeOnThisThread() changes.
class ExternalRamScope
{
public:
    ExternalRamScope();
    ~ExternalRamScope();
    ExternalRamScope(const ExternalRamScope&) = delete;
    ExternalRamScope& operator=(const ExternalRamScope&) = delete;

    static bool activeOnThisThread();

private:
    int slot_ = -1;
};

// Lowest free internal RAM between begin() and end(), which may run on two
// tasks: one config_update crosses ws_service, then the dispatcher (store and
// page rebuild). Monitoring is global, so one window at a time; a second begin()
// restarts it, and a window never ended is dropped by the next begin().
// No-op on the host build.
class InternalHeapWatch
{
public:
    static void begin();
    static void end(const char* operation);
};

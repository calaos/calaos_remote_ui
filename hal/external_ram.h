#pragma once

// While a scope lives, operator new on its thread prefers PSRAM whatever the
// size. The DOM of a big server message is thousands of small blocks that
// malloc serves from internal RAM first, the only RAM FreeRTOS objects can use.
// One owner at a time: a scope opened by another thread meanwhile is a no-op.
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
    bool owner_ = false;
};

// Lowest free internal RAM reached between construction and report(), for an
// operation that runs on one task. No-op on the host build.
class InternalHeapWatch
{
public:
    InternalHeapWatch();
    ~InternalHeapWatch();
    InternalHeapWatch(const InternalHeapWatch&) = delete;
    InternalHeapWatch& operator=(const InternalHeapWatch&) = delete;

    void report(const char* operation);

private:
    bool started_ = false;
};

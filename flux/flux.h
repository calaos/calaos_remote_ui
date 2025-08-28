#pragma once

#include "app_event.h"
#include "app_dispatcher.h"
#include "app_store.h"

// Initialize the Flux architecture
// This ensures the store subscribes to the dispatcher properly
inline void initFlux()
{
    // Initialize the store (this will automatically subscribe to the dispatcher)
    AppStore::getInstance();
}
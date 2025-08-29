# Common source file definitions for both ESP-IDF and Linux builds
# This file is included by both main/CMakeLists.txt (ESP-IDF) and CMakeLists.txt (Linux)

# Set policy for string comparisons to avoid warnings
cmake_policy(SET CMP0054 NEW)

# Main application sources
set(MAIN_SOURCES
    main/main.cpp
    main/app_main.cpp
    main/startup_page.cpp
    main/page_base.cpp
    main/stack_view.cpp
    main/test_page.cpp
    main/theme.cpp
    main/calaos_discovery.cpp
    main/lvgl_timer.cpp
    main/provisioning_crypto.cpp
    main/provisioning_manager.cpp
)

# Flux architecture sources
set(FLUX_SOURCES
    flux/app_dispatcher.cpp
    flux/app_store.cpp
)

# Network sources
set(NETWORK_SOURCES
    network/calaos_net.cpp
    network/udp/udp_client.cpp
    network/udp/udp_server.cpp
    network/http/http_client.cpp
    network/websocket/websocket_client.cpp
)

# HAL sources - ESP32 specific
set(ESP32_HAL_SOURCES
    hal/hal.cpp
    hal/esp32/esp32_hal.cpp
    hal/esp32/esp32_hal_display.cpp
    hal/esp32/esp32_hal_input.cpp
    hal/esp32/esp32_hal_network.cpp
    hal/esp32/esp32_hal_system.cpp
)

# HAL sources - Linux specific
set(LINUX_HAL_SOURCES
    hal/hal.cpp
    hal/linux/linux_hal.cpp
    hal/linux/linux_hal_display.cpp
    hal/linux/linux_hal_input.cpp
    hal/linux/linux_hal_network.cpp
    hal/linux/linux_hal_system.cpp
    hal/linux/display_backend_selector.cpp
)

# Common include directories
set(COMMON_INCLUDE_DIRS
    main
    hal
    flux
    network
)

# Platform-specific source selection
# For ESP-IDF builds, DETECTED_PLATFORM might not be set yet, so detect here
if(NOT DEFINED DETECTED_PLATFORM)
    # This happens in ESP-IDF builds where sources.cmake is included early
    if(DEFINED ENV{IDF_PATH})
        set(DETECTED_PLATFORM "ESP32")
    else()
        set(DETECTED_PLATFORM "LINUX")
    endif()
endif()

if(DETECTED_PLATFORM STREQUAL "ESP32")
    # ESP-IDF build - use ESP32 HAL
    set(PLATFORM_HAL_SOURCES ${ESP32_HAL_SOURCES})
    message(STATUS "Using ESP32 HAL sources")
elseif(DETECTED_PLATFORM STREQUAL "LINUX")
    # Linux build - use Linux HAL
    set(PLATFORM_HAL_SOURCES ${LINUX_HAL_SOURCES})
    message(STATUS "Using Linux HAL sources")
else()
    message(FATAL_ERROR "Unknown DETECTED_PLATFORM: ${DETECTED_PLATFORM}")
endif()

# Combined source list for the current platform
set(ALL_SOURCES
    ${MAIN_SOURCES}
    ${FLUX_SOURCES}
    ${NETWORK_SOURCES}
    ${PLATFORM_HAL_SOURCES}
)
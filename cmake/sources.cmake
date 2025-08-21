# Common source file definitions for both ESP-IDF and Linux builds
# This file is included by both main/CMakeLists.txt (ESP-IDF) and CMakeLists.txt (Linux)

# Main application sources
set(MAIN_SOURCES
    main/main.cpp
    main/app_main.cpp
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
)

# Platform-specific source selection
if(DEFINED ENV{IDF_PATH})
    # ESP-IDF build - use ESP32 HAL
    set(PLATFORM_HAL_SOURCES ${ESP32_HAL_SOURCES})
    message(STATUS "Using ESP32 HAL sources")
else()
    # Linux build - use Linux HAL  
    set(PLATFORM_HAL_SOURCES ${LINUX_HAL_SOURCES})
    message(STATUS "Using Linux HAL sources")
endif()

# Combined source list for the current platform
set(ALL_SOURCES 
    ${MAIN_SOURCES}
    ${PLATFORM_HAL_SOURCES}
)
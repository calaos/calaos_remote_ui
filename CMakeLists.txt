# ESP-IDF requires specific order of initialization
cmake_minimum_required(VERSION 3.16)

# Detect if we're in ESP-IDF environment and include project.cmake BEFORE project()
if(DEFINED ENV{IDF_PATH})
    # ESP-IDF build - must include project.cmake before project()
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)

    # Find Python for ESP-IDF
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    #LVGL custom config file setup
    idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE=1" APPEND)
    idf_build_set_property(COMPILE_OPTIONS "-I../main" APPEND)

    message(STATUS "Building for ESP32 platform")
endif()

# Define the project (works for both ESP-IDF and Linux)
project(calaos-remote-ui)

# Linux-specific configuration
if(NOT DEFINED ENV{IDF_PATH})
    message(STATUS "Building for Linux platform")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Linux-specific setup
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Find required packages for Linux
    find_package(PkgConfig REQUIRED)

    # Detect available display backends
    set(LV_USE_LINUX_FBDEV 0)
    set(LV_USE_LINUX_DRM 0)
    set(LV_USE_SDL 0)
    set(LV_USE_X11 0)
    set(LV_USE_OPENGLES 0)
    set(LV_USE_LINUX_EVDEV 0)
    set(LV_USE_LINUX_LIBINPUT 0)

    # Always enable framebuffer backend for Linux
    set(LV_USE_FBDEV 1)
    message(STATUS "Framebuffer backend support")

    # Check for DRM
    pkg_check_modules(LIBDRM QUIET libdrm)
    if(LIBDRM_FOUND)
        set(LV_USE_DRM 1)
        list(APPEND LINUX_DISPLAY_LIBS ${LIBDRM_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${LIBDRM_INCLUDE_DIRS})
        message(STATUS "DRM detected - enabling LV_USE_DRM")
    else()
        set(LV_USE_DRM 0)
        message(STATUS "DRM not found - LV_USE_DRM disabled")
    endif()

    pkg_check_modules(LIBGBM QUIET gbm)
    if(LIBGBM_FOUND)
        list(APPEND LINUX_DISPLAY_LIBS ${LIBGBM_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${LIBGBM_INCLUDE_DIRS})
        message(STATUS "GBM detected")
    endif()

    # Check for SDL2
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        set(LV_USE_SDL 1)
        list(APPEND LINUX_DISPLAY_LIBS ${SDL2_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${SDL2_INCLUDE_DIRS})
        message(STATUS "SDL2 detected - enabling LV_USE_SDL")
    else()
        set(LV_USE_SDL 0)
        message(STATUS "SDL2 not found - LV_USE_SDL disabled")
    endif()

    # Check for X11
    pkg_check_modules(X11 QUIET x11)
    if(X11_FOUND)
        set(LV_USE_X11 1)
        list(APPEND LINUX_DISPLAY_LIBS ${X11_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${X11_INCLUDE_DIR})
        message(STATUS "X11 detected - enabling LV_USE_X11")
    else()
        set(LV_USE_X11 0)
        message(STATUS "X11 not found - LV_USE_X11 disabled")
    endif()

    # Check for GLFW3
    pkg_check_modules(GLFW3 QUIET glfw3)
    if(GLFW3_FOUND)
        pkg_check_modules(GLEW REQUIRED glew)
        set(LV_USE_OPENGLES 1)
        list(APPEND LINUX_DISPLAY_LIBS ${GLFW3_LIBRARIES} ${GLEW_LIBRARIES})
        message(STATUS "GLFW3 detected - enabling LV_USE_OPENGLES")
    else()
        set(LV_USE_OPENGLES 0)
        message(STATUS "GLFW3 not found - LV_USE_OPENGLES disabled")
    endif()

    # Check for input backends
    pkg_check_modules(EVDEV QUIET libevdev)
    if(EVDEV_FOUND)
        set(LV_USE_EVDEV 1)
        list(APPEND LINUX_DISPLAY_LIBS ${EVDEV_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${EVDEV_INCLUDE_DIRS})
        message(STATUS "evdev input detected - enabling LV_USE_LINUX_EVDEV")
    else()
        set(LV_USE_EVDEV 0)
        message(STATUS "evdev input not found - LV_USE_LINUX_EVDEV disabled")
    endif()

    # Check for libinput
    pkg_check_modules(LIBINPUT QUIET libinput)
    if(LIBINPUT_FOUND)
        set(LV_USE_LIBINPUT 1)
        list(APPEND LINUX_DISPLAY_LIBS ${LIBINPUT_LIBRARIES})
        list(APPEND LINUX_DISPLAY_INCLUDES ${LIBINPUT_INCLUDE_DIRS})
        message(STATUS "libinput detected - enabling LV_USE_LINUX_LIBINPUT")
    else()
        set(LV_USE_LIBINPUT 0)
        message(STATUS "libinput not found - LV_USE_LINUX_LIBINPUT disabled")
    endif()

    # Generate platform configuration
    configure_file(
        ${CMAKE_SOURCE_DIR}/common/lv_conf_platform.h.in
        ${CMAKE_BINARY_DIR}/common/lv_conf_platform.h
        @ONLY
    )

    # Add LVGL
    add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)
    set (LV_CONF_BUILD_DISABLE_EXAMPLES ON)
    set (LV_CONF_BUILD_DISABLE_DEMOS ON)
    set (LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON)
    add_subdirectory(components/lvgl)

    # Add LVGL include directories for lv_conf.h/lv_conf_platform.h
    target_include_directories(lvgl PUBLIC
        ${CMAKE_BINARY_DIR}/common
        ${CMAKE_SOURCE_DIR}/main
    )

    # Create main executable
    add_executable(${PROJECT_NAME})

    # Add HAL sources
    file(GLOB_RECURSE HAL_SOURCES
        "hal/hal.cpp"
        "hal/linux/*.cpp"
    )

    # Add main sources
    set(MAIN_SOURCES
        main/main.cpp
        main/app_main.cpp
    )

    # Add common sources for Linux
    set(COMMON_SOURCES
        common/display_backend_selector.cpp
    )

    target_sources(${PROJECT_NAME} PRIVATE ${HAL_SOURCES} ${MAIN_SOURCES} ${COMMON_SOURCES})

    # Include directories
    target_include_directories(${PROJECT_NAME} PRIVATE
        main
        hal
        common
        ${CMAKE_BINARY_DIR}/common
        components/lvgl
        components/lvgl/src
        ${LINUX_DISPLAY_INCLUDES}
    )

    # Link libraries
    target_link_libraries(${PROJECT_NAME} lvgl pthread ${LINUX_DISPLAY_LIBS})
endif()

message(STATUS "--------------Compile Info------------")
message(STATUS "IDF_PATH = ${IDF_PATH}")
message(STATUS "IDF_TARGET = ${IDF_TARGET}")
message(STATUS "PROJECT_NAME = ${PROJECT_NAME}")
message(STATUS "PROJECT_DIR = ${PROJECT_DIR}")
message(STATUS "BUILD_DIR = ${BUILD_DIR}")
message(STATUS "SDKCONFIG = ${SDKCONFIG}")
message(STATUS "SDKCONFIG_DEFAULTS = ${SDKCONFIG_DEFAULTS}")
message(STATUS "CONFIG_LV_CONF_SKIP = ${CONFIG_LV_CONF_SKIP}")
message(STATUS "CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
message(STATUS "COMPILE_OPTIONS = ${COMPILE_OPTIONS}")
message(STATUS "--------------------------------------")
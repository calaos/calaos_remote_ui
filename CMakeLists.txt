# ESP-IDF requires specific order of initialization
cmake_minimum_required(VERSION 3.16)

# Set policies to avoid warnings
cmake_policy(SET CMP0054 NEW)

# Platform selection options
option(BUILD_LINUX "Force build for Linux platform" OFF)
option(BUILD_ESP "Force build for ESP32 platform" OFF)

# Platform detection and configuration
if(BUILD_LINUX AND BUILD_ESP)
    message(FATAL_ERROR "Cannot specify both BUILD_LINUX and BUILD_ESP")
elseif(BUILD_LINUX)
    set(DETECTED_PLATFORM "LINUX")
    message(STATUS "BUILD_LINUX enabled - targeting Linux platform")
elseif(BUILD_ESP)
    set(DETECTED_PLATFORM "ESP32")
    message(STATUS "BUILD_ESP enabled - targeting ESP32 platform")
else()
    # Auto-detect based on environment
    if(DEFINED ENV{IDF_PATH})
        set(DETECTED_PLATFORM "ESP32")
        message(STATUS "Auto-detected ESP-IDF environment - targeting ESP32")
    else()
        set(DETECTED_PLATFORM "LINUX")
        message(STATUS "Auto-detected Linux environment - targeting Linux")
    endif()
endif()

# ESP-IDF setup (only if targeting ESP32)
if(DETECTED_PLATFORM STREQUAL "ESP32")
    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR "ESP32 target selected but IDF_PATH environment variable not found")
    endif()
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
if(DETECTED_PLATFORM STREQUAL "LINUX")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Linux-specific setup
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

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

    # Check for libwebsockets (required)
    pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
    list(APPEND LINUX_DISPLAY_LIBS ${LIBWEBSOCKETS_LIBRARIES})
    list(APPEND LINUX_DISPLAY_INCLUDES ${LIBWEBSOCKETS_INCLUDE_DIRS})
    message(STATUS "libwebsockets detected - websocket support enabled")

    # Generate platform configuration
    configure_file(
        ${CMAKE_SOURCE_DIR}/hal/linux/lv_conf_platform.h.in
        ${CMAKE_BINARY_DIR}/hal/linux/lv_conf_platform.h
        @ONLY
    )

    # Add LVGL
    add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)
    set (LV_CONF_BUILD_DISABLE_EXAMPLES ON)
    set (LV_CONF_BUILD_DISABLE_DEMOS ON)
    set (LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON)
    add_subdirectory(components/lvgl)

    # Add smooth_ui_toolkit
    set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF)
    set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF)
    add_subdirectory(smooth_ui_toolkit)

    # Add LVGL include directories for lv_conf.h/lv_conf_platform.h
    target_include_directories(lvgl PUBLIC
        ${CMAKE_BINARY_DIR}/hal/linux
        ${CMAKE_SOURCE_DIR}/main
    )

    # Create main executable
    add_executable(${PROJECT_NAME})

    # Include shared source definitions
    include(${CMAKE_SOURCE_DIR}/cmake/sources.cmake)

    # For Linux, we can use the source lists directly since paths are relative to project root

    # PNG to C image conversion for Linux build
    set(IMAGE_DIR "${CMAKE_SOURCE_DIR}/main/images")
    set(CONVERTED_DIR "${CMAKE_BINARY_DIR}/images_build")
    file(MAKE_DIRECTORY ${CONVERTED_DIR})
    set(IMG_HEADER_FILE "${CONVERTED_DIR}/images_generated.h")
    file(WRITE ${IMG_HEADER_FILE} "#pragma once\n\n#include \"lvgl.h\"\n\n")
    set(LVGL_IMAGE_CONVERTER_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/LVGLImage.py")

    file(GLOB IMAGE_FILES "${IMAGE_DIR}/*.png")
    set(CONVERTED_IMAGE_FILES "")

    foreach(png_file ${IMAGE_FILES})
        get_filename_component(file_name ${png_file} NAME_WE)
        set(c_file "${CONVERTED_DIR}/${file_name}.c")

        add_custom_command(
            OUTPUT ${c_file}
            COMMAND ${Python3_EXECUTABLE} ${LVGL_IMAGE_CONVERTER_SCRIPT}
                --ofmt C
                --cf RGB565A8
                --output ${CONVERTED_DIR}
                ${png_file}
            DEPENDS ${png_file} ${LVGL_IMAGE_CONVERTER_SCRIPT}
            COMMENT "Converting ${png_file} to ${c_file}"
            VERBATIM
        )

        string(REPLACE "-" "_" var_name ${file_name})
        file(APPEND ${IMG_HEADER_FILE} "extern const lv_image_dsc_t ${var_name};\n")

        list(APPEND CONVERTED_IMAGE_FILES ${c_file})
    endforeach()

    target_sources(${PROJECT_NAME} PRIVATE ${ALL_SOURCES} ${CONVERTED_IMAGE_FILES})

    # Include directories
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${COMMON_INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR}/hal/linux
        ${CMAKE_BINARY_DIR}/images_build
        components/lvgl
        components/lvgl/src
        ${LINUX_DISPLAY_INCLUDES}
        ${CMAKE_SOURCE_DIR}/components/nlohmann-json/single_include/nlohmann
    )

    # Link libraries
    target_link_libraries(${PROJECT_NAME} lvgl smooth_ui_toolkit pthread ${LINUX_DISPLAY_LIBS})
endif()

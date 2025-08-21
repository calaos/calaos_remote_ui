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
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    
    # Find required packages for Linux
    find_package(PkgConfig REQUIRED)
    
    # Add LVGL
    add_subdirectory(components/lvgl)
    
    # Set compile definitions
    add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)
    
    # Create main executable
    add_executable(${PROJECT_NAME})
    
    # Add HAL sources
    file(GLOB_RECURSE HAL_SOURCES 
        "hal/*.cpp" 
        "hal/linux/*.cpp"
    )
    
    # Add main sources
    set(MAIN_SOURCES 
        main/main.cpp
        main/app_main.cpp
    )
    
    target_sources(${PROJECT_NAME} PRIVATE ${HAL_SOURCES} ${MAIN_SOURCES})
    
    # Include directories
    target_include_directories(${PROJECT_NAME} PRIVATE 
        main
        hal
        common
        components/lvgl
        components/lvgl/src
    )
    
    # Link libraries
    target_link_libraries(${PROJECT_NAME} lvgl pthread)
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
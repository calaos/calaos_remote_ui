# ESP-IDF component wrapper for smooth_ui_toolkit submodule
set(SMOOTH_UI_TOOLKIT_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../smooth_ui_toolkit)

# Src files
file(GLOB_RECURSE SMOOTH_UI_TOOLKIT_SRCS
    ${SMOOTH_UI_TOOLKIT_ROOT_DIR}/src/*.c
    ${SMOOTH_UI_TOOLKIT_ROOT_DIR}/src/*.cc
    ${SMOOTH_UI_TOOLKIT_ROOT_DIR}/src/*.cpp
)

# Include directories
set(SMOOTH_UI_TOOLKIT_INCS
    ${SMOOTH_UI_TOOLKIT_ROOT_DIR}/src/
    ${SMOOTH_UI_TOOLKIT_ROOT_DIR}/src/lvgl/
)

# Register component with compile definitions
idf_component_register(
    SRCS ${SMOOTH_UI_TOOLKIT_SRCS}
    INCLUDE_DIRS ${SMOOTH_UI_TOOLKIT_INCS}
    REQUIRES esp_lvgl_port
    PRIV_REQUIRES 
)

# Add compile definitions using ESP-IDF component interface
idf_component_get_property(component_lib ${COMPONENT_NAME} COMPONENT_LIB)
target_compile_definitions(${component_lib} PRIVATE SMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL)
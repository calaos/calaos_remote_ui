set(SRCS "")
list(APPEND SRCS
    "src/bsp_board_extra.c"
)

set(INCLUDE_DIRS "")
list(APPEND INCLUDE_DIRS "include")

foreach(SRC_DIR IN LISTS SRC_DIRS)
    file(GLOB_RECURSE SRC ${SRC_DIR}/*.c)
    list(APPEND SRCS ${SRC})
    list(APPEND INCLUDE_DIRS ${SRC_DIR}/include)
endforeach()

# Depend on whichever Waveshare BSP is active for this board.
# Both BSPs expose bsp/esp-bsp.h; EXCLUDE_COMPONENTS in the board cmake
# guarantees only one is in BUILD_COMPONENTS.
idf_build_get_property(_build_components BUILD_COMPONENTS)
set(BSP_DEPS "")
if("waveshare__esp32_p4_wifi6_touch_lcd_4b" IN_LIST _build_components)
    list(APPEND BSP_DEPS waveshare__esp32_p4_wifi6_touch_lcd_4b)
endif()
if("esp32_p4_wifi6_touch_lcd_x" IN_LIST _build_components)
    list(APPEND BSP_DEPS esp32_p4_wifi6_touch_lcd_x)
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES driver
    PRIV_REQUIRES esp_timer fatfs esp_psram esp_mm ${BSP_DEPS}
)

target_compile_options(
    ${COMPONENT_LIB} PRIVATE
        -Wno-format
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wunused-function
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-overflow
        -Wno-unused-but-set-variable
        -Wno-discarded-qualifiers)

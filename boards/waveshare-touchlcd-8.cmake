# Board: Waveshare ESP32-P4 Touch LCD 8"
# 800x1280 portrait MIPI DSI panel (JD9365), used in landscape (1280x800) via sw_rotate.

set(BOARD_NAME "waveshare-touchlcd-8")
set(BOARD_HARDWARE_ID "waveshare-touchlcd-8")
set(BOARD_PLATFORM "ESP32")
set(BOARD_MANUFACTURER "Waveshare")

# Display configuration (landscape, post-rotation)
set(BOARD_DISPLAY_WIDTH 1280)
set(BOARD_DISPLAY_HEIGHT 800)
set(BOARD_DISPLAY_COLOR_DEPTH 16)
set(BOARD_DISPLAY_ROTATION 90)
set(BOARD_PREFERED_GRID_HEIGHT 3)
set(BOARD_PREFERED_GRID_WIDTH 6)

# Capabilities
set(BOARD_HAS_TOUCHSCREEN true)
set(BOARD_HAS_WIFI true)
set(BOARD_HAS_ETHERNET false)

# Relay configuration
set(BOARD_RELAY_COUNT 0)
set(BOARD_RELAY_1_GPIO 0)
set(BOARD_RELAY_2_GPIO 0)

# Device provisioning config path (ESP32 uses partition table, not a file path)
set(BOARD_DEVICE_CONFIG_PATH "")

# OTA configuration - ESP32 native OTA
set(BOARD_OTA_BACKEND "esp32")
set(BOARD_OTA_BACKEND_TYPE "BOARD_OTA_BACKEND_ESP32")

# ESP-IDF specific configuration
set(BOARD_IDF_TARGET "esp32p4")

# sdkconfig defaults - applied in order (later files override earlier)
set(BOARD_SDKCONFIG_DEFAULTS
    "${CMAKE_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.esp32p4"
    "${CMAKE_SOURCE_DIR}/boards/waveshare-touchlcd-8/sdkconfig.defaults"
)

# Custom partition table with dual OTA
set(BOARD_PARTITION_TABLE "${CMAKE_SOURCE_DIR}/boards/waveshare-touchlcd-8/partitions.csv")

# Exclude the BSP used by waveshare-86-panel (and its ST7703 panel driver).
# Both BSPs export the same bsp_display_* symbols; only one can be linked.
set(EXCLUDE_COMPONENTS esp32_p4_wifi6_touch_lcd_4b esp_lcd_st7703)

message(STATUS "Board config: ${BOARD_NAME} (${BOARD_DISPLAY_WIDTH}x${BOARD_DISPLAY_HEIGHT}, OTA: ${BOARD_OTA_BACKEND})")

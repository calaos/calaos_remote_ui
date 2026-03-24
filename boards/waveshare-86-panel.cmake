# Board: Waveshare ESP32-P4 86-Panel
# 720x720 round display with ESP32-P4 chip

set(BOARD_NAME "waveshare-86-panel")
set(BOARD_HARDWARE_ID "waveshare-86-panel")
set(BOARD_PLATFORM "ESP32")
set(BOARD_MANUFACTURER "Waveshare")

# Display configuration
set(BOARD_DISPLAY_WIDTH 720)
set(BOARD_DISPLAY_HEIGHT 720)
set(BOARD_DISPLAY_COLOR_DEPTH 16)
set(BOARD_PREFERED_GRID_HEIGHT 3)
set(BOARD_PREFERED_GRID_WIDTH 3)

# Capabilities
set(BOARD_HAS_TOUCHSCREEN true)
set(BOARD_HAS_WIFI true)
set(BOARD_HAS_ETHERNET true)

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
    "${CMAKE_SOURCE_DIR}/boards/waveshare-86-panel/sdkconfig.defaults"
)

# Custom partition table with dual OTA
set(BOARD_PARTITION_TABLE "${CMAKE_SOURCE_DIR}/boards/waveshare-86-panel/partitions.csv")

message(STATUS "Board config: ${BOARD_NAME} (${BOARD_DISPLAY_WIDTH}x${BOARD_DISPLAY_HEIGHT}, OTA: ${BOARD_OTA_BACKEND})")

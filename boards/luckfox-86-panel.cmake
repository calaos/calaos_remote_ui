# Board: Luckfox 86-Panel
# 720x720 display with Luckfox Pico (Linux-based)

set(BOARD_NAME "luckfox-86-panel")
set(BOARD_HARDWARE_ID "luckfox-86-panel")
set(BOARD_PLATFORM "LINUX")
set(BOARD_MANUFACTURER "Luckfox")

# Display configuration
set(BOARD_DISPLAY_WIDTH 720)
set(BOARD_DISPLAY_HEIGHT 720)
set(BOARD_DISPLAY_COLOR_DEPTH 32)
set(BOARD_DISPLAY_ROTATION 0)
set(BOARD_PREFERED_GRID_HEIGHT 3)
set(BOARD_PREFERED_GRID_WIDTH 3)

# Capabilities
set(BOARD_HAS_TOUCHSCREEN true)
set(BOARD_HAS_WIFI true)
set(BOARD_HAS_ETHERNET true)

# Relay configuration
set(BOARD_RELAY_COUNT 2)
set(BOARD_RELAY_1_GPIO 0)  # TODO: Set correct GPIO pin from hardware schematic
set(BOARD_RELAY_2_GPIO 0)  # TODO: Set correct GPIO pin from hardware schematic

# Device provisioning config path (raw partition on eMMC/SD)
set(BOARD_DEVICE_CONFIG_PATH "/dev/mmcblk0p3")

# OTA configuration - Luckfox specific (stub for now)
set(BOARD_OTA_BACKEND "luckfox")
set(BOARD_OTA_BACKEND_TYPE "BOARD_OTA_BACKEND_LUCKFOX")

message(STATUS "Board config: ${BOARD_NAME} (${BOARD_DISPLAY_WIDTH}x${BOARD_DISPLAY_HEIGHT}, OTA: ${BOARD_OTA_BACKEND})")

# Board: Linux development with SDL
# Used for development and testing on desktop Linux

set(BOARD_NAME "linux-dev")
set(BOARD_HARDWARE_ID "linux-dev")
set(BOARD_PLATFORM "LINUX")
set(BOARD_MANUFACTURER "Calaos")

# Display configuration
set(BOARD_DISPLAY_WIDTH 720)
set(BOARD_DISPLAY_HEIGHT 720)
set(BOARD_DISPLAY_COLOR_DEPTH 32)
set(BOARD_PREFERED_GRID_HEIGHT 3)
set(BOARD_PREFERED_GRID_WIDTH 3)

# Capabilities
set(BOARD_HAS_TOUCHSCREEN true)
set(BOARD_HAS_WIFI false)
set(BOARD_HAS_ETHERNET true)

# Relay configuration (no relays on dev board)
set(BOARD_RELAY_COUNT 0)
set(BOARD_RELAY_1_GPIO -1)
set(BOARD_RELAY_2_GPIO -1)

# Device provisioning config path
# For development: read from a regular file in the user config directory
set(BOARD_DEVICE_CONFIG_PATH "")

# OTA configuration - disabled for development
set(BOARD_OTA_BACKEND "none")
set(BOARD_OTA_BACKEND_TYPE "BOARD_OTA_BACKEND_NONE")

message(STATUS "Board config: ${BOARD_NAME} (${BOARD_DISPLAY_WIDTH}x${BOARD_DISPLAY_HEIGHT}, OTA: ${BOARD_OTA_BACKEND})")

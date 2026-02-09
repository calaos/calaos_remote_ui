#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include "hal.h"

namespace CalaosProtocol
{

// Protocol message types
inline constexpr const char* MSG_IO_STATES = "remote_ui_io_states";
inline constexpr const char* MSG_IO_STATE = "io_state";
inline constexpr const char* MSG_CONFIG_UPDATE = "remote_ui_config_update";
inline constexpr const char* MSG_SET_STATE = "set_state";
inline constexpr const char* MSG_GET_CONFIG = "remote_ui_get_config";
inline constexpr const char* MSG_EVENT = "event";
inline constexpr const char* MSG_FW_UPDATE_AVAILABLE = "remote_ui_fw_update_available";

// WebSocket endpoint
inline constexpr const char* WS_ENDPOINT = "/api/v3/remote_ui/ws";
inline constexpr int WS_PORT = 5454;

// Authentication headers
inline constexpr const char* AUTH_HEADER_TOKEN = "Authorization";
inline constexpr const char* AUTH_HEADER_TIMESTAMP = "X-Auth-Timestamp";
inline constexpr const char* AUTH_HEADER_NONCE = "X-Auth-Nonce";
inline constexpr const char* AUTH_HEADER_HMAC = "X-Auth-HMAC";

// Firmware info headers
inline constexpr const char* FW_HEADER_VERSION = "X-Device-Version";
inline constexpr const char* FW_HEADER_HARDWARE_ID = "X-Device-Hardware-Id";

/**
 * @brief Structure representing a widget configuration in the grid
 */
struct WidgetConfig
{
    std::string io_id;      // IO unique identifier (e.g., "io_0")
    std::string type;       // Widget type (e.g., "LightSwitch", "Temperature")
    int x = 0;              // Grid position X
    int y = 0;              // Grid position Y
    int w = 1;              // Grid width
    int h = 1;              // Grid height

    WidgetConfig() = default;

    WidgetConfig(const std::string& io_id,
                const std::string& type,
                int x, int y, int w, int h):
        io_id(io_id),
        type(type),
        x(x),
        y(y),
        w(w),
        h(h)
    {
    }
};

/**
 * @brief Structure representing a page with widgets
 */
struct PageConfig
{
    std::vector<WidgetConfig> widgets;

    PageConfig() = default;
};

/**
 * @brief Structure representing the complete pages configuration
 */
struct PagesConfig
{
    int grid_width = HAL::getInstance().getDisplay().getPreferedGridWidth();
    int grid_height = HAL::getInstance().getDisplay().getPreferedGridHeight();
    std::vector<PageConfig> pages;

    PagesConfig() = default;

    // Parse from JSON string
    static PagesConfig fromJson(const std::string& json_str);
};

/**
 * @brief Structure representing an IO (Input/Output) object state
 */
struct IoState
{
    std::string id;         // IO unique identifier
    std::string type;       // IO type (light, temp, switch, etc.)
    std::string state;      // Current state value (for booleans: "true"/"false")
    std::string gui_type;   // GUI widget type
    std::string name;       // Display name
    int brightness = -1;    // Brightness value (0-100) for light_dimmer, -1 if not applicable
    bool visible = true;    // Visibility flag
    bool enabled = true;    // Enabled/disabled flag

    IoState() = default;

    IoState(const std::string& id,
            const std::string& type,
            const std::string& state,
            const std::string& gui_type,
            const std::string& name):
        id(id),
        type(type),
        state(state),
        gui_type(gui_type),
        name(name)
    {
    }
};

/**
 * @brief Structure representing remote UI configuration
 */
struct RemoteUIConfig
{
    std::string name;           // Screen name
    std::string room;           // Room assignment
    std::string theme;          // Theme (dark/light)
    int brightness = 80;        // Screen brightness (0-100)
    int timeout = 30;           // Screen timeout in seconds
    std::string pages_json;     // Pages configuration as JSON string

    RemoteUIConfig() = default;

    /**
     * @brief Parse pages_json and return PagesConfig
     * @return Parsed pages configuration
     */
    PagesConfig getParsedPages() const
    {
        if (pages_json.empty())
            return PagesConfig();

        return PagesConfig::fromJson(pages_json);
    }
};

} // namespace CalaosProtocol

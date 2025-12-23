#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace CalaosProtocol
{

// Protocol message types
static const char* MSG_IO_STATES = "remote_ui_io_states";
static const char* MSG_IO_STATE = "io_state";
static const char* MSG_CONFIG_UPDATE = "remote_ui_config_update";
static const char* MSG_SET_STATE = "set_state";
static const char* MSG_GET_CONFIG = "remote_ui_get_config";
static const char* MSG_EVENT = "event";

// WebSocket endpoint
static const char* WS_ENDPOINT = "/api/v3/remote_ui/ws";
static const int WS_PORT = 5454;

// Authentication headers
static const char* AUTH_HEADER_TOKEN = "Authorization";
static const char* AUTH_HEADER_TIMESTAMP = "X-Auth-Timestamp";
static const char* AUTH_HEADER_NONCE = "X-Auth-Nonce";
static const char* AUTH_HEADER_HMAC = "X-Auth-HMAC";

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
    int grid_width = 3;     // Default 3x3 grid
    int grid_height = 3;
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
    std::string state;      // Current state value
    std::string gui_type;   // GUI widget type
    std::string name;       // Display name
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

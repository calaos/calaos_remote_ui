#pragma once

#include <string>
#include <map>

namespace CalaosProtocol
{

// Protocol message types
static const char* MSG_IO_STATES = "remote_ui_io_states";
static const char* MSG_IO_STATE = "io_state";
static const char* MSG_CONFIG_UPDATE = "remote_ui_config_update";
static const char* MSG_SET_STATE = "set_state";
static const char* MSG_GET_CONFIG = "remote_ui_get_config";

// WebSocket endpoint
static const char* WS_ENDPOINT = "/api/v3/remote_ui/ws";
static const int WS_PORT = 5454;

// Authentication headers
static const char* AUTH_HEADER_TOKEN = "Authorization";
static const char* AUTH_HEADER_TIMESTAMP = "X-Auth-Timestamp";
static const char* AUTH_HEADER_NONCE = "X-Auth-Nonce";
static const char* AUTH_HEADER_HMAC = "X-Auth-HMAC";

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
};

} // namespace CalaosProtocol

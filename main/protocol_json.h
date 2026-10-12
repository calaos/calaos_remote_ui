#pragma once

// Tolerant readers for the server's JSON. The server is not consistent about types
// (flags as "true" or true, states as strings, numbers or booleans), and one
// mistyped field must cost that field, never the surrounding message.

#include "calaos_protocol.h"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <optional>
#include <string>

namespace CalaosProtocol
{
namespace wire
{

inline const nlohmann::json* find(const nlohmann::json& obj, const char* key)
{
    if (!obj.is_object())
        return nullptr;
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

inline bool scalarToString(const nlohmann::json& v, std::string& out)
{
    if (v.is_string())
        out = v.get<std::string>();
    else if (v.is_boolean())
        out = v.get<bool>() ? "true" : "false";
    else if (v.is_number())
        out = v.dump();
    else
        return false;
    return true;
}

// Absent or non-scalar gives `def`.
inline std::string stringOr(const nlohmann::json& obj, const char* key, const std::string& def = "")
{
    std::string out;
    const nlohmann::json* v = find(obj, key);
    return (v && scalarToString(*v, out)) ? out : def;
}

// Identifiers are never coerced: a numeric id is a protocol error, not an id.
inline bool idField(const nlohmann::json& obj, const char* key, std::string& out)
{
    const nlohmann::json* v = find(obj, key);
    if (!v || !v->is_string())
        return false;
    out = v->get<std::string>();
    return !out.empty();
}

// The server sends "true"/"false" strings; any other string is false.
inline bool flagOr(const nlohmann::json& obj, const char* key, bool def)
{
    const nlohmann::json* v = find(obj, key);
    if (!v)
        return def;
    if (v->is_boolean())
        return v->get<bool>();
    if (v->is_string())
        return v->get<std::string>() == "true";
    return def;
}

inline std::optional<int> toInt(const nlohmann::json& v)
{
    if (v.is_number_integer() || v.is_number_unsigned())
    {
        const long long n = v.get<long long>();
        if (n < INT_MIN || n > INT_MAX)
            return std::nullopt;
        return static_cast<int>(n);
    }

    if (v.is_number_float())
    {
        const double d = v.get<double>();
        if (!std::isfinite(d) || d < INT_MIN || d > INT_MAX)
            return std::nullopt;
        return static_cast<int>(d);
    }

    if (v.is_string())
    {
        const std::string& s = v.get<std::string>();
        char* end = nullptr;
        errno = 0;
        const long n = std::strtol(s.c_str(), &end, 10);
        if (s.empty() || end == s.c_str() || *end != '\0' || errno == ERANGE || n < INT_MIN || n > INT_MAX)
            return std::nullopt;
        return static_cast<int>(n);
    }

    return std::nullopt;
}

inline std::optional<int> intField(const nlohmann::json& obj, const char* key)
{
    const nlohmann::json* v = find(obj, key);
    return v ? toInt(*v) : std::nullopt;
}

inline std::string objectToString(const nlohmann::json& obj, const char* key)
{
    const nlohmann::json* v = find(obj, key);
    if (!v || !v->is_object())
        return "{}";
    return v->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// A device without a family cannot be routed to a widget, so it is ignored.
inline std::optional<DeviceInfo> deviceField(const nlohmann::json& obj)
{
    const nlohmann::json* d = find(obj, "device");
    if (!d || !d->is_object())
        return std::nullopt;

    DeviceInfo info;
    info.family = stringOr(*d, "family");
    if (info.family.empty())
        return std::nullopt;

    info.impl = stringOr(*d, "impl");
    info.capabilities = objectToString(*d, "capabilities");
    info.state = objectToString(*d, "state");
    info.ios = objectToString(*d, "ios");
    return info;
}

// Reads the fields shared by the batch forms of remote_ui_io_states.
inline void readIoFields(const nlohmann::json& obj, IoState& io)
{
    io.type = stringOr(obj, "type");
    io.state = stringOr(obj, "state");
    io.gui_type = stringOr(obj, "gui_type");
    io.name = stringOr(obj, "name");
    io.visible = flagOr(obj, "visible", true);
    io.enabled = flagOr(obj, "enabled", true);
    io.partial = !find(obj, "visible") && !find(obj, "enabled");
    io.device = deviceField(obj);
}

} // namespace wire
} // namespace CalaosProtocol

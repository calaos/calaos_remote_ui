#include "calaos_protocol.h"
#include "protocol_json.h"
#include "logging.h"
#include <nlohmann/json.hpp>

static const char* TAG = "protocol";

using json = nlohmann::json;

namespace CalaosProtocol
{

namespace
{

// Coordinates may be numbers or numeric strings; anything else rejects the widget.
bool readCell(const json& widget, const char* key, int& out)
{
    const json* v = wire::find(widget, key);
    if (!v)
        return true;

    auto n = wire::toInt(*v);
    if (!n)
        return false;

    out = *n;
    return true;
}

// Short key wins over its long alias.
bool readCell(const json& widget, const char* key, const char* alias, int& out)
{
    return readCell(widget, wire::find(widget, key) ? key : alias, out);
}

std::optional<WidgetConfig> parseWidget(const json& widget_json)
{
    if (!widget_json.is_object())
        return std::nullopt;

    WidgetConfig widget;

    for (const char* key : {"io_id", "type", "name"})
    {
        const json* v = wire::find(widget_json, key);
        if (v && !v->is_string())
            return std::nullopt;
    }

    widget.io_id = wire::stringOr(widget_json, "io_id");
    widget.type = wire::stringOr(widget_json, "type");
    widget.override_name = wire::stringOr(widget_json, "name");

    if (!readCell(widget_json, "x", widget.x) ||
        !readCell(widget_json, "y", widget.y) ||
        !readCell(widget_json, "w", "width", widget.w) ||
        !readCell(widget_json, "h", "height", widget.h))
        return std::nullopt;

    static const std::vector<std::string> knownKeys = {
        "io_id", "type", "name", "x", "y", "w", "h", "width", "height"
    };
    for (auto it = widget_json.begin(); it != widget_json.end(); ++it)
    {
        const std::string& key = it.key();
        bool isKnown = false;
        for (const auto& k : knownKeys)
        {
            if (key == k)
            {
                isKnown = true;
                break;
            }
        }
        if (isKnown)
            continue;

        std::string value;
        if (wire::scalarToString(it.value(), value))
            widget.params[key] = std::move(value);
    }

    return widget;
}

} // namespace

PagesConfig PagesConfig::fromJson(const std::string& json_str)
{
    PagesConfig config;

    try
    {
        json j = json::parse(json_str);

        if (auto w = wire::intField(j, "grid_width"))
            config.grid_width = *w;
        else if (wire::find(j, "grid_width"))
            ESP_LOGW(TAG, "Unusable grid_width, keeping %d", config.grid_width);

        if (auto h = wire::intField(j, "grid_height"))
            config.grid_height = *h;
        else if (wire::find(j, "grid_height"))
            ESP_LOGW(TAG, "Unusable grid_height, keeping %d", config.grid_height);

        // Validate grid dimensions
        if (config.grid_width < 1)
        {
            ESP_LOGW(TAG, "Invalid grid_width: %d, using default 3", config.grid_width);
            config.grid_width = 3;
        }

        if (config.grid_height < 1)
        {
            ESP_LOGW(TAG, "Invalid grid_height: %d, using default 3", config.grid_height);
            config.grid_height = 3;
        }

        const json* pages = wire::find(j, "pages");
        if (pages && pages->is_array())
        {
            for (const auto& page_json : *pages)
            {
                PageConfig page;

                const json* widgets = wire::find(page_json, "widgets");
                if (widgets && widgets->is_array())
                {
                    for (const auto& widget_json : *widgets)
                    {
                        auto parsed = parseWidget(widget_json);
                        if (!parsed)
                        {
                            ESP_LOGW(TAG, "Skipping widget with a mistyped field");
                            continue;
                        }

                        WidgetConfig& widget = *parsed;

                        if (widget.type.empty())
                        {
                            ESP_LOGW(TAG, "Skipping widget with empty type");
                            continue;
                        }

                        if (widget.w < 1 || widget.h < 1)
                        {
                            ESP_LOGW(TAG, "Skipping widget %s with invalid size: %dx%d",
                                    widget.io_id.c_str(), widget.w, widget.h);
                            continue;
                        }

                        page.widgets.push_back(std::move(widget));
                    }
                }

                config.pages.push_back(std::move(page));
            }
        }

        ESP_LOGI(TAG, "Parsed pages config: grid=%dx%d, pages=%d",
                config.grid_width, config.grid_height, (int)config.pages.size());
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Failed to parse pages JSON: %s", e.what());
        return PagesConfig();
    }

    return config;
}

} // namespace CalaosProtocol

#include "calaos_protocol.h"
#include "logging.h"
#include <nlohmann/json.hpp>

static const char* TAG = "protocol";

using json = nlohmann::json;

namespace CalaosProtocol
{

PagesConfig PagesConfig::fromJson(const std::string& json_str)
{
    PagesConfig config;

    try
    {
        json j = json::parse(json_str);

        // Parse grid dimensions (use defaults if not present)
        if (j.contains("grid_width"))
            config.grid_width = j["grid_width"].get<int>();

        if (j.contains("grid_height"))
            config.grid_height = j["grid_height"].get<int>();

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

        // Parse pages array
        if (j.contains("pages") && j["pages"].is_array())
        {
            for (const auto& page_json : j["pages"])
            {
                PageConfig page;

                // Parse widgets array in this page
                if (page_json.contains("widgets") && page_json["widgets"].is_array())
                {
                    for (const auto& widget_json : page_json["widgets"])
                    {
                        WidgetConfig widget;

                        // Parse widget fields
                        if (widget_json.contains("io_id"))
                            widget.io_id = widget_json["io_id"].get<std::string>();

                        if (widget_json.contains("type"))
                            widget.type = widget_json["type"].get<std::string>();

                        // Parse x, y, w, h (can be int or string)
                        if (widget_json.contains("x"))
                        {
                            if (widget_json["x"].is_string())
                                widget.x = std::stoi(widget_json["x"].get<std::string>());
                            else
                                widget.x = widget_json["x"].get<int>();
                        }

                        if (widget_json.contains("y"))
                        {
                            if (widget_json["y"].is_string())
                                widget.y = std::stoi(widget_json["y"].get<std::string>());
                            else
                                widget.y = widget_json["y"].get<int>();
                        }

                        if (widget_json.contains("w"))
                        {
                            if (widget_json["w"].is_string())
                                widget.w = std::stoi(widget_json["w"].get<std::string>());
                            else
                                widget.w = widget_json["w"].get<int>();
                        }
                        else if (widget_json.contains("width"))
                        {
                            if (widget_json["width"].is_string())
                                widget.w = std::stoi(widget_json["width"].get<std::string>());
                            else
                                widget.w = widget_json["width"].get<int>();
                        }

                        if (widget_json.contains("h"))
                        {
                            if (widget_json["h"].is_string())
                                widget.h = std::stoi(widget_json["h"].get<std::string>());
                            else
                                widget.h = widget_json["h"].get<int>();
                        }
                        else if (widget_json.contains("height"))
                        {
                            if (widget_json["height"].is_string())
                                widget.h = std::stoi(widget_json["height"].get<std::string>());
                            else
                                widget.h = widget_json["height"].get<int>();
                        }

                        // Validate widget
                        if (widget.io_id.empty())
                        {
                            ESP_LOGW(TAG, "Skipping widget with empty io_id");
                            continue;
                        }

                        if (widget.type.empty())
                        {
                            ESP_LOGW(TAG, "Skipping widget %s with empty type", widget.io_id.c_str());
                            continue;
                        }

                        if (widget.w < 1 || widget.h < 1)
                        {
                            ESP_LOGW(TAG, "Skipping widget %s with invalid size: %dx%d",
                                    widget.io_id.c_str(), widget.w, widget.h);
                            continue;
                        }

                        page.widgets.push_back(widget);
                    }
                }

                config.pages.push_back(page);
            }
        }

        ESP_LOGI(TAG, "Parsed pages config: grid=%dx%d, pages=%d",
                config.grid_width, config.grid_height, (int)config.pages.size());
    }
    catch (const json::exception& e)
    {
        ESP_LOGE(TAG, "Failed to parse pages JSON: %s", e.what());
        // Return default config
        return PagesConfig();
    }

    return config;
}

} // namespace CalaosProtocol

#pragma once

#include <nlohmann/json.hpp>
#include <string>

// Same shape and size as a real panel config with a Climate widget: 63 IOs, 48
// of them the role IOs of climate devices, and 4 pages.
inline nlohmann::json prodSizedConfig()
{
    using nlohmann::json;
    json ioItems = json::array();
    for (int i = 0; i < 15; i++)
        ioItems.push_back({{"id", "prod_io_" + std::to_string(i)}, {"type", "WODigital"}, {"gui_type", "light"},
                           {"io_type", "output"}, {"name", "Lumière " + std::to_string(i)}, {"visible", "true"},
                           {"rw", "true"}});
    for (int i = 0; i < 48; i++)
        ioItems.push_back({{"id", "prod_role_" + std::to_string(i)}, {"type", "InternalInt"}, {"gui_type", "var_int"},
                           {"io_type", "inout"}, {"name", "Chauffage role " + std::to_string(i)},
                           {"visible", "false"}, {"rw", "true"}});

    json pages = json::array();
    const int widgetsPerPage[] = {8, 5, 4, 1};
    int n = 0;
    for (int count : widgetsPerPage)
    {
        json widgets = json::array();
        for (int w = 0; w < count; w++, n++)
            widgets.push_back({{"type", n == 2 ? "Climate" : "LightSwitch"}, {"io_id", "prod_io_" + std::to_string(n % 15)},
                               {"name", "Widget é " + std::to_string(n)}, {"x", w % 3}, {"y", w / 3},
                               {"w", "1"}, {"h", "1"}});
        pages.push_back({{"name", "Page"}, {"type", "default"}, {"widgets", widgets}});
    }

    return {{"name", "UI Porte"}, {"room", "Entrée"}, {"grid_width", 3}, {"grid_height", 3},
            {"screensaver_mode", "clock"}, {"screensaver_timeout", "60"}, {"io_items", ioItems}, {"pages", pages}};
}

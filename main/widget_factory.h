#pragma once

#include "calaos_widget.h"
#include <memory>
#include <functional>
#include <map>
#include <string>

/**
 * @brief Factory for creating widgets based on type and size
 *
 * Uses the Factory pattern with registration mechanism.
 * If a widget type/size is not registered, creates a WidgetError instead.
 */
class WidgetFactory
{
public:
    /**
     * @brief Get singleton instance
     */
    static WidgetFactory& getInstance();

    /**
     * @brief Create a widget based on configuration
     * @param parent Parent LVGL object
     * @param config Widget configuration
     * @param gridInfo Grid layout information
     * @return Widget instance (or WidgetError if type/size not supported)
     */
    std::unique_ptr<CalaosWidget> createWidget(
        lv_obj_t* parent,
        const CalaosProtocol::WidgetConfig& config,
        const GridLayoutInfo& gridInfo
    );

    /**
     * @brief Function signature for widget creators
     */
    using WidgetCreator = std::function<std::unique_ptr<CalaosWidget>(
        lv_obj_t*,
        const CalaosProtocol::WidgetConfig&,
        const GridLayoutInfo&
    )>;

    /**
     * @brief Register a widget type with specific size
     * @param typeName Widget type name (e.g., "LightSwitch")
     * @param width Widget width in grid units
     * @param height Widget height in grid units
     * @param creator Function that creates the widget
     */
    void registerWidget(const std::string& typeName,
                       int width,
                       int height,
                       WidgetCreator creator);

    /**
     * @brief Check if a widget type/size is registered
     * @param typeName Widget type name
     * @param width Widget width in grid units
     * @param height Widget height in grid units
     * @return true if registered
     */
    bool isRegistered(const std::string& typeName, int width, int height) const;

private:
    WidgetFactory();

    // Delete copy/move constructors for singleton
    WidgetFactory(const WidgetFactory&) = delete;
    WidgetFactory& operator=(const WidgetFactory&) = delete;
    WidgetFactory(WidgetFactory&&) = delete;
    WidgetFactory& operator=(WidgetFactory&&) = delete;

    /**
     * @brief Register all built-in widgets
     */
    void registerBuiltinWidgets();

    /**
     * @brief Create a key for the creators map
     * @param type Widget type
     * @param w Width in grid units
     * @param h Height in grid units
     * @return Key string (format: "Type_WxH")
     */
    std::string makeKey(const std::string& type, int w, int h) const;

    // Map of widget creators
    // Key format: "LightSwitch_1x1", "Temperature_1x1", etc.
    std::map<std::string, WidgetCreator> creators;
};

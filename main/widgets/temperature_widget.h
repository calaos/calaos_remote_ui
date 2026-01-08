#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "lvgl.h"

/**
 * @brief Temperature widget (1x1 grid size)
 *
 * Read-only temperature display widget:
 * - icon_temp icon at top
 * - Temperature value in °C (yellow color)
 * - IO name at bottom (blue color)
 * - Column flex layout, centered
 * - Styled like LightSwitch OFF state (non-clickable)
 */
class TemperatureWidget : public CalaosWidget
{
public:
    TemperatureWidget(lv_obj_t* parent,
                     const CalaosProtocol::WidgetConfig& config,
                     const GridLayoutInfo& gridInfo);

    ~TemperatureWidget() override = default;

protected:
    /**
     * @brief Update UI when IO state changes
     */
    void onStateUpdate(const CalaosProtocol::IoState& state) override;

private:
    /**
     * @brief Create UI elements
     */
    void createUI();

    /**
     * @brief Parse temperature string and format display
     * @param tempStr Temperature string from server
     * @return Formatted temperature string (e.g., "21.50°C" or "-- °C")
     */
    std::string formatTemperature(const std::string& tempStr);

    // UI elements
    lv_obj_t* iconImage;
    lv_obj_t* tempLabel;
    lv_obj_t* nameLabel;
};

#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "shutter_common.h"
#include "lvgl.h"

/**
 * @brief Shutter widget (1x1 grid size)
 *
 * Supports both "shutter" (bool) and "shutter_smart" (string) gui_types.
 *
 * Shutter simple:
 * - Icon: icon_shutter_on (open) / icon_shutter_off (closed)
 * - Click sends "toggle"
 *
 * Shutter smart:
 * - Animated visual: part_shutter (fixed frame) + part_shutter2 (moving part)
 * - part_shutter2 moves vertically based on percent (0=open, 100=closed)
 * - Click sends "toggle"
 */
class ShutterWidget : public CalaosWidget
{
public:
    ShutterWidget(lv_obj_t* parent,
                  const CalaosProtocol::WidgetConfig& config,
                  const GridLayoutInfo& gridInfo);

    ~ShutterWidget() override;

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
     * @brief Check if this is a smart shutter
     */
    bool isSmartShutter() const;

    /**
     * @brief Parse simple shutter state (bool)
     */
    bool parseIsOpen(const std::string& stateStr) const;

    /**
     * @brief Update visual state for simple shutter
     */
    void updateSimpleVisual(bool isOpen);

    /**
     * @brief Update visual state for smart shutter
     */
    void updateSmartVisual(const ShutterSmartState& smartState);

    /**
     * @brief LVGL click event callback
     */
    static void clickEventCb(lv_event_t* e);

    /**
     * @brief Handle click event
     */
    void onClicked();

    // UI elements
    lv_obj_t* iconImage;        // Icon for simple shutter
    lv_obj_t* nameLabel;        // IO name at bottom

    // Smart shutter visual elements
    lv_obj_t* shutterContainer; // Clip container for smart visual
    lv_obj_t* shutterFrame;     // part_shutter (fixed frame)
    lv_obj_t* shutterMoving;    // part_shutter2 (moving part)
};

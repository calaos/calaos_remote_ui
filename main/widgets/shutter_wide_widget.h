#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "shutter_common.h"
#include "lvgl.h"

/**
 * @brief Shutter widget for 2x1 grid size
 *
 * Supports both "shutter" (bool) and "shutter_smart" (string) gui_types.
 *
 * Layout:
 * - Top row: Icon/visual (left) + 2 buttons Up/Down in row (right)
 * - Bottom: Name label
 *
 * No click action on widget itself, only button actions:
 * - Up: sends "up"
 * - Down: sends "down"
 */
class ShutterWideWidget : public CalaosWidget
{
public:
    ShutterWideWidget(lv_obj_t* parent,
                      const CalaosProtocol::WidgetConfig& config,
                      const GridLayoutInfo& gridInfo);

    ~ShutterWideWidget() override;

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
     * @brief Create the 2 action buttons (Up, Down)
     */
    void createButtons(lv_obj_t* parent);

    /**
     * @brief Button event callbacks
     */
    static void upButtonCb(lv_event_t* e);
    static void downButtonCb(lv_event_t* e);

    // UI elements
    lv_obj_t* topContainer;     // Container for icon + buttons (row)
    lv_obj_t* iconImage;        // Icon for simple shutter
    lv_obj_t* nameLabel;        // IO name at bottom

    // Smart shutter visual elements
    lv_obj_t* shutterContainer; // Clip container for smart visual
    lv_obj_t* shutterFrame;     // part_shutter (fixed frame)
    lv_obj_t* shutterMoving;    // part_shutter2 (moving part)

    // Buttons
    lv_obj_t* btnUp;
    lv_obj_t* btnDown;
};

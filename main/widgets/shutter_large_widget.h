#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "shutter_common.h"
#include "lvgl.h"

/**
 * @brief Shutter widget for large sizes (3x1, 4x1)
 *
 * Supports both "shutter" (bool) and "shutter_smart" (string) gui_types.
 *
 * Layout:
 * - Top row: Icon/visual (left) + Status text zone (center) + 3 buttons Up/Stop/Down (right)
 * - Bottom: Name label
 *
 * Status text zone shows:
 * - Line 1: State text (e.g., "State: Opened", "State: 50% Closed")
 * - Line 2: Action text (e.g., "Stopped", "Closing...")
 *
 * No click action on widget itself, only button actions:
 * - Up: sends "up"
 * - Stop: sends "stop"
 * - Down: sends "down"
 */
class ShutterLargeWidget : public CalaosWidget
{
public:
    ShutterLargeWidget(lv_obj_t* parent,
                       const CalaosProtocol::WidgetConfig& config,
                       const GridLayoutInfo& gridInfo);

    ~ShutterLargeWidget() override;

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
     * @brief Update the status text labels
     */
    void updateStatusText(const ShutterSmartState& smartState);

    /**
     * @brief Create the 3 action buttons (Up, Stop, Down)
     */
    void createButtons(lv_obj_t* parent);

    /**
     * @brief Button event callbacks
     */
    static void upButtonCb(lv_event_t* e);
    static void stopButtonCb(lv_event_t* e);
    static void downButtonCb(lv_event_t* e);

    // UI elements
    lv_obj_t* topContainer;     // Container for icon + text + buttons (row)
    lv_obj_t* iconImage;        // Icon for simple shutter
    lv_obj_t* nameLabel;        // IO name at bottom

    // Status text labels
    lv_obj_t* stateTextLabel;   // "State: Opened" / "State: 50% Closed"
    lv_obj_t* actionTextLabel;  // "Stopped" / "Closing..."

    // Smart shutter visual elements
    lv_obj_t* shutterContainer; // Clip container for smart visual
    lv_obj_t* shutterFrame;     // part_shutter (fixed frame)
    lv_obj_t* shutterMoving;    // part_shutter2 (moving part)

    // Buttons
    lv_obj_t* btnUp;
    lv_obj_t* btnStop;
    lv_obj_t* btnDown;
};

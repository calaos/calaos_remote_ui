#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "lvgl.h"
#include <memory>

// Forward declaration
class ImageSequenceAnimator;

/**
 * @brief Light switch widget (1x1 grid size)
 *
 * Custom design:
 * - ON/OFF icon (bulb or power symbol)
 * - IO name at bottom
 * - Rectangle with border radius 10px, border theme_color_blue
 * - Click changes background color
 */
class LightSwitchWidget : public CalaosWidget
{
public:
    LightSwitchWidget(lv_obj_t* parent,
                     const CalaosProtocol::WidgetConfig& config,
                     const GridLayoutInfo& gridInfo);

    ~LightSwitchWidget() override;

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
     * @brief Update visual state (colors, icon)
     * @param isOn true if light is ON
     */
    void updateVisualState(bool isOn);

    /**
     * @brief LVGL click event callback
     */
    static void clickEventCb(lv_event_t* e);

    /**
     * @brief Handle click event
     */
    void onClicked();

    // UI elements
    lv_obj_t* iconImage;    // Animated icon image
    lv_obj_t* nameLabel;    // IO name
    std::unique_ptr<ImageSequenceAnimator> lightAnimator;

    bool updatingFromServer = false;  // Prevent feedback loop
};

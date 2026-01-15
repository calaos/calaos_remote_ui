#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "lvgl.h"
#include <memory>

// Forward declaration
class ImageSequenceAnimator;

/**
 * @brief Light switch widget for horizontal sizes (2x1, 3x1, 4x1)
 *
 * Layout:
 * - For dimmers: Icon (left) + Name/State (right) + Slider (bottom)
 * - For regular lights: Icon (left) + Name/State (right), no slider
 *
 * Click on widget toggles ON/OFF
 * Slider (for dimmers) controls brightness 0-100%
 */
class LightSwitchWideWidget : public CalaosWidget
{
public:
    LightSwitchWideWidget(lv_obj_t* parent,
                          const CalaosProtocol::WidgetConfig& config,
                          const GridLayoutInfo& gridInfo);

    ~LightSwitchWideWidget() override;

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
     * @brief Check if this widget controls a dimmer
     */
    bool isDimmer() const;

    /**
     * @brief Parse state string to determine if light is ON
     */
    bool parseIsOn(const std::string& stateStr) const;

    /**
     * @brief Get brightness percentage from state
     */
    int getBrightness(const std::string& stateStr) const;

    /**
     * @brief Update visual state (colors, icon)
     */
    void updateVisualState(bool isOn);

    /**
     * @brief Update state label text
     */
    void updateStateLabel(bool isOn, int brightness);

    /**
     * @brief LVGL click event callback
     */
    static void clickEventCb(lv_event_t* e);

    /**
     * @brief LVGL slider released event callback
     */
    static void sliderReleasedCb(lv_event_t* e);

    /**
     * @brief Handle click event
     */
    void onClicked();

    /**
     * @brief Handle slider released
     */
    void onSliderReleased();

    // UI elements
    lv_obj_t* topContainer;     // Container for icon and text
    lv_obj_t* iconImage;        // Animated icon image
    lv_obj_t* textContainer;    // Container for name and state labels
    lv_obj_t* nameLabel;        // IO name
    lv_obj_t* stateLabel;       // State text (Off or XX%)
    lv_obj_t* slider;           // Brightness slider (only for dimmers)
    std::unique_ptr<ImageSequenceAnimator> lightAnimator;

    bool updatingFromServer = false;  // Prevent feedback loop
    bool wasOn = false;               // Track previous ON state for animation
};

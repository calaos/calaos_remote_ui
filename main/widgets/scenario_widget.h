#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include <memory>

// Forward declarations
struct _lv_timer_t;

namespace smooth_ui_toolkit {
namespace color {
    struct AnimateRgb_t;
}
}

/**
 * @brief Scenario widget (1x1 grid size)
 *
 * Custom design:
 * - Scenario icon (static)
 * - IO name at bottom in blue
 * - Rectangle with OFF state styling
 * - Click sends "true" action and triggers animation
 * - Animation: bump blue->yellow, wait 400ms, fade yellow->blue
 */
class ScenarioWidget : public CalaosWidget
{
public:
    ScenarioWidget(lv_obj_t* parent,
                   const CalaosProtocol::WidgetConfig& config,
                   const GridLayoutInfo& gridInfo);

    ~ScenarioWidget() override;

    /**
     * @brief Called from page render loop to update animations
     */
    void render() override;

protected:
    /**
     * @brief Update UI when IO state changes (scenarios don't receive events)
     */
    void onStateUpdate(const CalaosProtocol::IoState& state) override;

private:
    /**
     * @brief Create UI elements
     */
    void createUI();

    /**
     * @brief LVGL press event callback
     */
    static void pressEventCb(lv_event_t* e);

    /**
     * @brief Handle press event
     */
    void onPressed();

    /**
     * @brief LVGL click event callback
     */
    static void clickEventCb(lv_event_t* e);

    /**
     * @brief Handle click event
     */
    void onClicked();

    /**
     * @brief Start animation sequence
     */
    void startAnimation();

    /**
     * @brief Animation phase 1 complete callback
     */
    void onBumpComplete();

    /**
     * @brief Delay timer callback for phase 2
     */
    static void delayTimerCb(lv_timer_t* timer);

    /**
     * @brief Start fade animation (phase 2)
     */
    void startFadeAnimation();

    /**
     * @brief Animation phase 2 complete callback
     */
    void onFadeComplete();

    // UI elements
    lv_obj_t* iconImage;    // Static scenario icon
    lv_obj_t* nameLabel;    // IO name

    // Animation state
    std::unique_ptr<smooth_ui_toolkit::color::AnimateRgb_t> labelColorAnim;
    std::unique_ptr<smooth_ui_toolkit::color::AnimateRgb_t> bgColorAnim;
    bool isAnimating;
    int animationPhase;     // 0=idle, 1=bump, 2=waiting, 3=fade
    lv_timer_t* delayTimer; // Timer for 400ms delay between phases
};

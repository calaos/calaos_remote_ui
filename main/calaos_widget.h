#pragma once

#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "calaos_protocol.h"
#include "app_store.h"
#include <string>
#include <memory>
#include <functional>

/**
 * @brief Grid layout information for widget positioning
 */
struct GridLayoutInfo
{
    int gridWidth;      // Total number of columns (e.g., 3)
    int gridHeight;     // Total number of rows (e.g., 3)
    int screenWidth;    // Screen width in pixels (720)
    int screenHeight;   // Screen height in pixels (720)
    int cellWidth;      // Pixel width of one grid cell
    int cellHeight;     // Pixel height of one grid cell
    int padding = 8;    // Padding between widgets in pixels
};

/**
 * @brief Base class for all Calaos widgets
 *
 * Handles:
 * - Grid-based positioning and sizing
 * - AppStore subscription for IO state updates
 * - Sending state changes via WebSocket
 * - Thread-safe UI updates with display lock
 */
class CalaosWidget : public smooth_ui_toolkit::lvgl_cpp::Container
{
public:
    /**
     * @brief Construct a new Calaos Widget
     * @param parent Parent LVGL object
     * @param config Widget configuration (io_id, type, position, size)
     * @param gridInfo Grid layout information
     */
    CalaosWidget(lv_obj_t* parent,
                 const CalaosProtocol::WidgetConfig& config,
                 const GridLayoutInfo& gridInfo);

    virtual ~CalaosWidget();

    /**
     * @brief Get widget configuration
     */
    const CalaosProtocol::WidgetConfig& getConfig() const { return config; }

    /**
     * @brief Get IO identifier
     */
    const std::string& getIoId() const { return config.io_id; }

    /**
     * @brief Called from page render loop to update animations
     * Child classes override this to update their animations
     */
    virtual void render() {}

protected:
    /**
     * @brief Send state change to server (called by child classes)
     * @param newState New state value
     * @return true if message sent successfully
     */
    bool sendStateChange(const std::string& newState);

    /**
     * @brief Child classes override this to implement widget-specific UI updates
     * @param state New IO state
     */
    virtual void onStateUpdate(const CalaosProtocol::IoState& state) = 0;

    // Configuration
    CalaosProtocol::WidgetConfig config;
    GridLayoutInfo gridInfo;

    // Current IO state
    CalaosProtocol::IoState currentState;

private:
    /**
     * @brief Calculate pixel position from grid coordinates and apply to widget
     */
    void calculateAndApplyPosition();

    /**
     * @brief Subscribe to AppStore state changes
     */
    void subscribeToStateChanges();

    /**
     * @brief Called when AppStore state changes
     * @param appState New application state
     */
    void onAppStateChanged(const AppState& appState);

    // Subscription ID for unsubscribing
    SubscriptionId subscriptionId_;
};

#pragma once

#include "../calaos_widget.h"
#include "smooth_ui_toolkit.h"

/**
 * @brief Error widget displayed when a widget type/size is not supported
 *
 * Shows a visual error indicator with the unsupported type and size.
 * This widget supports all sizes since it's a fallback.
 */
class WidgetError : public CalaosWidget
{
public:
    /**
     * @brief Construct a new Widget Error
     * @param parent Parent LVGL object
     * @param config Widget configuration
     * @param gridInfo Grid layout information
     * @param errorMessage Error message to display
     */
    WidgetError(lv_obj_t* parent,
               const CalaosProtocol::WidgetConfig& config,
               const GridLayoutInfo& gridInfo,
               const std::string& errorMessage);

    ~WidgetError() override;

protected:
    /**
     * @brief No-op for error widget (doesn't respond to state updates)
     */
    void onStateUpdate(const CalaosProtocol::IoState& state) override;

private:
    void createUI();

    std::string errorMessage;
    lv_obj_t* warningIcon;
    lv_obj_t* errorLabel;
    lv_obj_t* typeLabel;
    lv_obj_t* sizeLabel;
};

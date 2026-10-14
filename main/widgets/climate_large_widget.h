#pragma once

#include "../calaos_widget.h"
#include "climate_detail.h"
#include "climate_device.h"
#include "lvgl.h"
#include <memory>

/**
 * Climate widget for 2x2 cells and larger (spec UI_CLIMATE.md): the whole climate
 * inside the widget, with no view in between. Mode pill and settings button on
 * top, the 300 degree dial and the "- 22 +" line in the middle, the title at the
 * bottom like the other widgets. Only the mode and the settings open popups.
 *
 * The content and its commands are those of the full-screen view (ClimateDetail),
 * laid out for the widget's size.
 */
class ClimateLargeWidget : public CalaosWidget
{
public:
    ClimateLargeWidget(lv_obj_t* parent,
                       const CalaosProtocol::WidgetConfig& config,
                       const GridLayoutInfo& gridInfo);
    ~ClimateLargeWidget() override;

protected:
    void onStateUpdate(const CalaosProtocol::IoState& state) override;
    void onCommandRefused() override;

private:
    climate::DeviceReader reader_;
    std::unique_ptr<ClimateDetail> panel_;

    friend struct ClimateLargeWidgetTestAccess;
};

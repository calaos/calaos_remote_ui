#pragma once

#include "../calaos_widget.h"
#include "../lvgl_timer.h"
#include "climate_detail.h"
#include "climate_device.h"
#include "climate_dial.h"
#include "climate_present.h"
#include "lvgl.h"
#include <memory>
#include <string>

/**
 * Climate widget for the 1x1, 2x1 and 1x2 cells (spec UI_CLIMATE.md): read only,
 * a tap opens the full-screen view where the setpoint, the mode and the other
 * settings are changed.
 *
 * - 1x1: room temperature, "> setpoint", status, name
 * - 2x1, 1x2: a mini dial carrying the setpoint, the name, the status with the
 *   relay's wait gauge, and the room temperature
 */
class ClimateSmallWidget : public CalaosWidget
{
public:
    ClimateSmallWidget(lv_obj_t* parent,
                       const CalaosProtocol::WidgetConfig& config,
                       const GridLayoutInfo& gridInfo);
    ~ClimateSmallWidget() override;

protected:
    void onStateUpdate(const CalaosProtocol::IoState& state) override;
    void onCommandRefused() override;

private:
    void build();
    void buildCompact(lv_obj_t* body);
    void buildWide(lv_obj_t* body);
    void buildTall(lv_obj_t* body);
    void buildMiniDial(lv_obj_t* parent, int diameter, int stroke, int knob);
    void buildStatus(lv_obj_t* parent, const lv_font_t* font);
    void refresh();
    void fitName();
    std::string structureKey() const;

    void open();
    void detailClosed();
    static void tapCb(lv_event_t* e);
    static void closeCb(void* self);

    climate::DeviceReader reader_;
    climate::LockoutClock clock_;
    climate::Layout layout_;
    std::string builtKey_;
    int contentW_ = 0;
    int contentH_ = 0;

    std::unique_ptr<LvglTimer> secondTimer_;
    std::unique_ptr<ClimateDetail> detail_;

    lv_obj_t* body_ = nullptr;
    std::unique_ptr<ClimateDial> dial_;
    lv_obj_t* nameLabel_ = nullptr;
    const lv_font_t* nameFont_ = nullptr;
    int nameLines_ = 2;
    int nameWidth_ = 0;
    lv_obj_t* roomLabel_ = nullptr;      // 1x1: the big reading; elsewhere "Pièce 25.4°"
    lv_obj_t* setLabel_ = nullptr;       // 1x1: "22.5°"
    lv_obj_t* setRow_ = nullptr;
    lv_obj_t* statusIcon_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    lv_obj_t* gauge_ = nullptr;
    lv_obj_t* valueLabel_ = nullptr;     // in the dial
    lv_obj_t* lowLabel_ = nullptr;
    lv_obj_t* highLabel_ = nullptr;

    friend struct ClimateSmallWidgetTestAccess;
};

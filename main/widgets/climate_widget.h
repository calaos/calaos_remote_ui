#pragma once

#include "../calaos_widget.h"
#include "../lvgl_timer.h"
#include "climate_logic.h"
#include "lvgl.h"
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Climate device widget, any grid size (spec §9.3)
 *
 * Driven only by the device's capabilities and state, never by its impl:
 * - 1x1: setpoint, current temperature, action, -/+ and a mode button that cycles
 * - 2x1, 1x2, 2x2 and larger: mode buttons, setpoint or range with -/+ per
 *   handle, fan and swing choices when the device has them
 *
 * Taps are coalesced into one command, sent once the user pauses.
 */
class ClimateWidget : public CalaosWidget
{
public:
    ClimateWidget(lv_obj_t* parent,
                  const CalaosProtocol::WidgetConfig& config,
                  const GridLayoutInfo& gridInfo);

    ~ClimateWidget() override;

    // A pause this long after the last tap sends the command
    static constexpr uint32_t kCoalesceMs = 700;
    // A sent value not echoed by then gives way to the server's
    static constexpr uint32_t kEchoTimeoutMs = 4000;

protected:
    void onStateUpdate(const CalaosProtocol::IoState& state) override;
    void onCommandRefused() override;

private:
    enum class Op
    {
        TargetDown, TargetUp,
        LowDown, LowUp,
        HighDown, HighUp,
        ShiftDown, ShiftUp,
        CycleMode, CycleFan, CycleSwing,
        Mode, Fan, Swing
    };

    struct Binding
    {
        ClimateWidget* widget;
        Op op;
        std::string value;
    };

    struct Chip
    {
        std::string value;
        lv_obj_t* obj;
        lv_obj_t* label;
    };

    void readState(const CalaosProtocol::IoState& state);
    std::string structureKey() const;
    void build();
    void buildCompact(lv_obj_t* body);
    void buildWide(lv_obj_t* body);
    void buildTall(lv_obj_t* body);
    void buildLarge(lv_obj_t* body);
    int columnHeight(lv_obj_t* column) const;
    void fitToHeight();
    void refresh();

    lv_obj_t* addButton(lv_obj_t* parent, bool plus, Op op, int size);
    lv_obj_t* addChip(lv_obj_t* parent, const std::string& text, Op op, const std::string& value,
                      std::vector<Chip>* list);
    lv_obj_t* addRangeRow(lv_obj_t* parent, lv_obj_t** valueLabel, Op down, Op up,
                          const lv_font_t* font, int buttonSize);
    void addModeChips(lv_obj_t* parent, int width);
    void addOptionRow(lv_obj_t* parent, const char* caption, const std::vector<std::string>& options,
                      Op op, std::vector<Chip>* chips);
    lv_obj_t* addInfoRow(lv_obj_t* parent);

    static void eventCb(lv_event_t* e);
    void handle(Op op, const std::string& value);
    void scheduleFlush();
    void flush(bool repaint = true);

    climate::Controller controller_;
    climate::Capabilities caps_;
    std::string parsedCaps_;
    std::string parsedIos_;
    climate::Layout layout_;
    bool hasDevice_ = false;
    std::string builtKey_;
    int contentW_ = 0;
    int contentH_ = 0;

    std::deque<Binding> bindings_;
    std::unique_ptr<LvglTimer> flushTimer_;
    std::unique_ptr<LvglTimer> echoTimer_;

    lv_obj_t* body_ = nullptr;
    lv_obj_t* nameLabel_ = nullptr;
    lv_obj_t* currentLabel_ = nullptr;
    lv_obj_t* actionLabel_ = nullptr;
    lv_obj_t* singleBox_ = nullptr;
    lv_obj_t* targetLabel_ = nullptr;
    lv_obj_t* degreeLabel_ = nullptr;
    lv_obj_t* rangeBox_ = nullptr;
    lv_obj_t* lowLabel_ = nullptr;
    lv_obj_t* highLabel_ = nullptr;
    lv_obj_t* noneLabel_ = nullptr;
    lv_obj_t* modeCycle_ = nullptr;
    lv_obj_t* modeCycleLabel_ = nullptr;
    lv_obj_t* fanCycleLabel_ = nullptr;
    lv_obj_t* swingCycleLabel_ = nullptr;

    std::vector<lv_obj_t*> stepButtons_;      // greyed with the setpoint
    std::vector<lv_obj_t*> shiftButtons_;     // compact -/+: hidden without a setpoint
    std::vector<lv_obj_t*> optional_;         // dropped, last first, when the cell is too short
    std::vector<Chip> modeChips_;
    std::vector<Chip> fanChips_;
    std::vector<Chip> swingChips_;
    std::vector<std::pair<Op, lv_obj_t*>> buttons_;

    friend struct ClimateWidgetTestAccess;
};

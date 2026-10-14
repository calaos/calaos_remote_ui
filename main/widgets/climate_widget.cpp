#include "climate_widget.h"
#include "../theme.h"
#include "logging.h"
#include <algorithm>

static const char* TAG = "widget.climate";

using namespace climate;

namespace
{

const lv_color_t kChipBg = LV_COLOR_MAKE(0x26, 0x26, 0x26);
const lv_color_t kCaption = LV_COLOR_MAKE(0x8A, 0x8A, 0x8A);

// Only sizes other screens already link: each new font costs 40-150 KB of flash
const std::vector<const lv_font_t*> kMidFonts = {&roboto_light_48, &roboto_regular_32, &roboto_regular_28, &roboto_regular_24};

constexpr int kGap = 8;
// 48 px and up: about 5 mm on the 4" 720x720 panel, the smallest target for a finger
constexpr int kChipHeight = 48;
constexpr int kButton = 52;
constexpr int kLargeButton = 56;

lv_style_selector_t when(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(state);
}

lv_color_t modeColor(const std::string& mode)
{
    if (mode == "heat") return theme_color_orange;
    if (mode == "cool") return theme_color_blue;
    if (mode == "auto") return theme_color_green;
    if (mode == "dry") return theme_color_yellow;
    if (mode == "fan_only") return theme_color_white;
    return theme_color_grey;
}

lv_color_t actionColor(const std::string& action)
{
    if (action == "heating") return theme_color_orange;
    if (action == "cooling") return theme_color_blue;
    if (action == "drying") return theme_color_yellow;
    if (action == "fan") return theme_color_white;
    return theme_color_grey;
}

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "")
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

int32_t textWidth(const char* text, const lv_font_t* font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// Largest font in which `count` copies of text, gap apart, fit in width
const lv_font_t* fit(const std::vector<const lv_font_t*>& fonts, const std::string& text,
                     int width, int count = 1)
{
    for (const lv_font_t* f : fonts)
        if (count * textWidth(text.c_str(), f) + (count - 1) * kGap * 2 <= width)
            return f;
    return fonts.back();
}

void setEnabled(lv_obj_t* o, bool enabled)
{
    if (enabled)
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    else
        lv_obj_add_state(o, LV_STATE_DISABLED);
}

void setVisible(lv_obj_t* o, bool visible)
{
    if (!o)
        return;
    if (visible)
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void setText(lv_obj_t* l, const std::string& text)
{
    if (l && text != lv_label_get_text(l))
        lv_label_set_text(l, text.c_str());
}

std::string join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v)
        out += s + ",";
    return out;
}

} // namespace

ClimateWidget::ClimateWidget(lv_obj_t* parent,
                             const CalaosProtocol::WidgetConfig& config,
                             const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo)
{
    ESP_LOGI(TAG, "Creating climate widget: %s (size %dx%d)", config.io_id.c_str(), config.w, config.h);

    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);

    constexpr int pad = 16;
    setPadding(pad, pad, pad, pad);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_SCROLLABLE);

    contentW_ = config.w * gridInfo.cellWidth - 2 * gridInfo.padding - 2 * pad - 4;
    contentH_ = config.h * gridInfo.cellHeight - 2 * gridInfo.padding - 2 * pad - 4;

    // Created paused: resumed by a tap, paused again when fired
    flushTimer_ = std::make_unique<LvglTimer>([this]() { flush(); }, kCoalesceMs);
    flushTimer_->pause();
    echoTimer_ = std::make_unique<LvglTimer>([this]()
    {
        echoTimer_->pause();
        controller_.expireSent();
        refresh();
    }, kEchoTimeoutMs);
    echoTimer_->pause();

    readState(currentState);
    build();
    refresh();
}

ClimateWidget::~ClimateWidget()
{
    // A page rebuilt mid-edit must not lose the user's last taps
    if (controller_.hasPending())
        flush(false);
}

void ClimateWidget::readState(const CalaosProtocol::IoState& state)
{
    hasDevice_ = state.device.has_value();
    if (hasDevice_)
    {
        // Capabilities rarely change; the state does at every io_changed
        if (state.device->capabilities != parsedCaps_ || state.device->ios != parsedIos_)
        {
            parsedCaps_ = state.device->capabilities;
            parsedIos_ = state.device->ios;
            caps_ = Capabilities::parse(parsedCaps_, parsedIos_);
        }
        controller_.update(caps_, Values::parse(state.device->state, state.state));
    }
    else
        controller_.update(Capabilities{}, Values{});
}

std::string ClimateWidget::structureKey() const
{
    const Capabilities& c = controller_.caps();
    return join(c.modes) + "|" + join(c.fanModes) + "|" + join(c.swingModes) + "|" +
           (c.setpoint ? "s" : "") + (c.single ? "1" : "") + (c.range ? "r" : "") +
           (c.reportsAction ? "a" : "") + (hasDevice_ ? "d" : "") + "|" +
           formatSetpoint(c.bounds.max, c.bounds.step);
}

void ClimateWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    readState(state);
    if (structureKey() != builtKey_)
        build();
    refresh();
}

void ClimateWidget::onCommandRefused()
{
    ESP_LOGW(TAG, "Command refused for %s, back to the server state", config.io_id.c_str());
    flushTimer_->pause();
    echoTimer_->pause();
    controller_.refused();
    refresh();
}

void ClimateWidget::build()
{
    builtKey_ = structureKey();

    if (body_)
        lv_obj_delete(body_);
    bindings_.clear();
    stepButtons_.clear();
    optional_.clear();
    buttons_.clear();
    degreeLabel_ = nullptr;
    modeChips_.clear();
    fanChips_.clear();
    swingChips_.clear();
    nameLabel_ = currentLabel_ = actionLabel_ = nullptr;
    singleBox_ = targetLabel_ = rangeBox_ = lowLabel_ = highLabel_ = noneLabel_ = nullptr;
    fanCycleLabel_ = swingCycleLabel_ = nullptr;

    body_ = box(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(body_, kGap, 0);
    lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    buildLarge(body_);

    fitToHeight();
}

int ClimateWidget::columnHeight(lv_obj_t* column) const
{
    // The setpoint slot changes with the mode: count it at its tallest
    const Capabilities& c = controller_.caps();
    const std::pair<lv_obj_t*, bool> slot[] = {
        {singleBox_, c.single},
        {rangeBox_, c.range && c.setpoint},
        {noneLabel_, !c.setpoint || c.hasMode("fan_only")},
    };

    int32_t used = 0;
    int32_t slotHeight = -1;
    int count = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(column); i++)
    {
        lv_obj_t* child = lv_obj_get_child(column, i);
        bool inSlot = false;
        for (const auto& [obj, possible] : slot)
        {
            if (child != obj)
                continue;
            inSlot = true;
            if (possible)
                slotHeight = std::max(slotHeight, lv_obj_get_height(child));
        }
        if (inSlot || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
            continue;
        // A spacer only takes what is left, but its gaps are there
        if (lv_obj_get_style_flex_grow(child, LV_PART_MAIN) == 0)
            used += lv_obj_get_height(child);
        count++;
    }
    if (slotHeight >= 0)
    {
        used += slotHeight;
        count++;
    }
    return used + std::max(0, count - 1) * static_cast<int>(lv_obj_get_style_pad_row(column, LV_PART_MAIN));
}

void ClimateWidget::fitToHeight()
{
    // Built with every setpoint variant shown, so each can be measured
    lv_obj_update_layout(get());
    for (auto it = optional_.rbegin(); it != optional_.rend(); ++it)
    {
        lv_obj_t* column = lv_obj_get_parent(*it);
        const int used = columnHeight(column);
        if (used <= contentH_)
            continue;

        ESP_LOGI(TAG, "%s: %d px for %d, dropping an optional row", config.io_id.c_str(), used, contentH_);
        lv_obj_add_flag(*it, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(get());
    }
}

lv_obj_t* ClimateWidget::addButton(lv_obj_t* parent, bool plus, Op op, int size)
{
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, kChipBg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, theme_color_widget_bg_on, when(LV_STATE_PRESSED));
    lv_obj_set_style_border_color(b, theme_color_blue, when(LV_STATE_PRESSED));
    lv_obj_set_style_border_width(b, 2, when(LV_STATE_PRESSED));
    lv_obj_set_style_opa(b, LV_OPA_40, when(LV_STATE_DISABLED));

    // Bars, not glyphs: Roboto's hyphen is too short to read as a minus
    const int length = size * 2 / 5;
    const int thickness = size >= kLargeButton ? 4 : 3;
    for (int i = 0; i < (plus ? 2 : 1); i++)
    {
        lv_obj_t* bar = lv_obj_create(b);
        lv_obj_remove_style_all(bar);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(bar, i ? thickness : length, i ? length : thickness);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_color(bar, theme_color_white, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_center(bar);
    }

    bindings_.push_back({this, op, ""});
    lv_obj_add_event_cb(b, eventCb, LV_EVENT_CLICKED, &bindings_.back());
    buttons_.push_back({op, b});
    return b;
}

lv_obj_t* ClimateWidget::addChip(lv_obj_t* parent, const std::string& text, Op op,
                                 const std::string& value, std::vector<Chip>* list)
{
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, LV_SIZE_CONTENT, kChipHeight);
    lv_obj_set_style_min_width(c, 56, 0);
    lv_obj_set_style_pad_hor(c, 10, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_bg_color(c, kChipBg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, theme_color_blue, when(LV_STATE_PRESSED));
    lv_obj_set_style_border_width(c, 2, when(LV_STATE_PRESSED));
    lv_obj_set_style_opa(c, LV_OPA_40, when(LV_STATE_DISABLED));

    lv_obj_t* l = label(c, &roboto_regular_22, theme_color_white, text.c_str());
    lv_obj_center(l);

    bindings_.push_back({this, op, value});
    lv_obj_add_event_cb(c, eventCb, LV_EVENT_CLICKED, &bindings_.back());
    if (list)
        list->push_back({value, c, l});
    return c;
}

lv_obj_t* ClimateWidget::addRangeRow(lv_obj_t* parent, lv_obj_t** valueLabel, Op down, Op up,
                                     const lv_font_t* font, int buttonSize)
{
    lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    stepButtons_.push_back(addButton(row, false, down, buttonSize));
    *valueLabel = label(row, font, theme_color_white, "--");
    stepButtons_.push_back(addButton(row, true, up, buttonSize));
    return row;
}

void ClimateWidget::addModeChips(lv_obj_t* parent, int width)
{
    const auto& modes = controller_.caps().modes;
    if (modes.empty())
        return;

    constexpr int gap = 6;
    lv_obj_t* wrap = box(parent, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_width(wrap, width);
    lv_obj_set_style_pad_row(wrap, gap, 0);
    lv_obj_set_style_pad_column(wrap, gap, 0);

    // Equal chips in balanced rows: 6 modes on two rows go 3 + 3, not 5 + 1
    int chipW = 56;
    for (const auto& m : modes)
        chipW = std::max<int>(chipW, textWidth(modeLabel(m).c_str(), &roboto_regular_22) + 20);
    const int n = static_cast<int>(modes.size());
    const int fits = std::max(1, (width + gap) / (chipW + gap));
    const int rows = (n + fits - 1) / fits;
    const int perRow = (n + rows - 1) / rows;
    const int w = (width - (perRow - 1) * gap) / perRow;

    for (const auto& m : modes)
        lv_obj_set_width(addChip(wrap, modeLabel(m), Op::Mode, m, &modeChips_), w);
}

void ClimateWidget::addOptionRow(lv_obj_t* parent, const char* caption,
                                 const std::vector<std::string>& options, Op op, std::vector<Chip>* chips)
{
    if (options.empty())
        return;

    lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* cap = label(row, &roboto_regular_22, kCaption, caption);
    lv_obj_set_width(cap, 72);

    // Every choice when they fit, else one button that steps through them
    int needed = 72;
    for (const auto& o : options)
        needed += std::max<int>(56, textWidth(optionLabel(o).c_str(), &roboto_regular_22) + 20) + 6;

    if (needed <= contentW_)
    {
        for (const auto& o : options)
            lv_obj_set_flex_grow(addChip(row, optionLabel(o), op, o, chips), 1);
    }
    else
    {
        lv_obj_t* chip = addChip(row, "--", op == Op::Fan ? Op::CycleFan : Op::CycleSwing, "", nullptr);
        lv_obj_set_flex_grow(chip, 1);
        lv_obj_t* l = lv_obj_get_child(chip, 0);
        if (op == Op::Fan) fanCycleLabel_ = l; else swingCycleLabel_ = l;
    }
    optional_.push_back(row);
}

lv_obj_t* ClimateWidget::addInfoRow(lv_obj_t* parent)
{
    lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    currentLabel_ = label(row, &roboto_regular_22, theme_color_white);
    actionLabel_ = label(row, &roboto_regular_22, theme_color_grey);
    return row;
}

void ClimateWidget::buildLarge(lv_obj_t* body)
{
    const Bounds& b = controller_.caps().bounds;
    const std::string widest = formatSetpoint(b.max, b.step);

    lv_obj_t* header = box(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_pad_column(header, kGap, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    nameLabel_ = label(header, &roboto_regular_24, theme_color_blue);
    lv_obj_set_flex_grow(nameLabel_, 1);
    lv_label_set_long_mode(nameLabel_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    actionLabel_ = label(header, &roboto_regular_24, theme_color_grey);

    // The number large, its degree small and raised; the 80 px font has no degree glyph
    const int bigW = contentW_ - 2 * kLargeButton - 4 * kGap - 24;
    const std::string digits = widest.substr(0, widest.size() - std::string("°").size());
    // A device with few controls gets a larger number rather than an empty tile
    const Capabilities& caps = controller_.caps();
    const bool sparse = caps.fanModes.empty() && caps.swingModes.empty();
    std::vector<const lv_font_t*> bigFonts = {&roboto_regular_80, &roboto_regular_60};
    if (sparse)
        bigFonts.insert(bigFonts.begin(), {&roboto_regular_120, &roboto_regular_100});
    singleBox_ = addRangeRow(body, &targetLabel_, Op::TargetDown, Op::TargetUp,
                             fit(bigFonts, digits, bigW), kLargeButton);
    lv_obj_set_style_pad_hor(singleBox_, kGap, 0);
    lv_obj_t* number = box(singleBox_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(number, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_move_to_index(number, 1);
    lv_obj_set_parent(targetLabel_, number);
    degreeLabel_ = label(number, &roboto_light_48, theme_color_white, "°");
    noneLabel_ = label(body, &roboto_regular_60, theme_color_white, "--");

    // Each handle under its own caption: low is where heating starts, high cooling
    rangeBox_ = box(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(rangeBox_, LV_PCT(100));
    lv_obj_set_flex_align(rangeBox_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int halfW = (contentW_ - 2 * kGap) / 2;
    const lv_font_t* rangeFont = fit(kMidFonts, widest,
                                     halfW - 2 * kButton - 2 * kGap);
    const struct { const char* caption; lv_obj_t** value; Op down; Op up; } handles[] = {
        {"Heat to", &lowLabel_, Op::LowDown, Op::LowUp},
        {"Cool to", &highLabel_, Op::HighDown, Op::HighUp},
    };
    for (const auto& h : handles)
    {
        lv_obj_t* col = box(rangeBox_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(col, halfW);
        lv_obj_set_style_pad_row(col, 4, 0);
        label(col, &roboto_regular_22, kCaption, h.caption);
        addRangeRow(col, h.value, h.down, h.up, rangeFont, kButton);
    }

    currentLabel_ = label(body, &roboto_regular_24, theme_color_white);

    addModeChips(body, contentW_);
    addOptionRow(body, "Fan", controller_.caps().fanModes, Op::Fan, &fanChips_);
    addOptionRow(body, "Swing", controller_.caps().swingModes, Op::Swing, &swingChips_);
}

void ClimateWidget::refresh()
{
    const Values v = controller_.view();
    const SetpointKind kind = controller_.kind();
    const bool editable = controller_.canEditSetpoint();
    const bool active = hasDevice_ && controller_.caps().hasMode(v.mode) && v.mode != "off";
    const lv_color_t accent = modeColor(v.mode);

    setBgColor(active ? theme_color_widget_bg_on : theme_color_widget_bg_off);
    setBorderColor(active ? accent : theme_color_widget_border_off);

    setText(nameLabel_, getDisplayName());

    if (currentLabel_)
    {
        std::string current = "Room " + formatTemperature(v.current);
        // With no setpoint the big slot shows the room temperature, named here
        if (kind == SetpointKind::None)
            current = "Room";
        setText(currentLabel_, current);
        lv_obj_set_style_text_color(currentLabel_, kind == SetpointKind::None ? kCaption : theme_color_white, 0);
    }

    if (actionLabel_)
    {
        const bool shown = controller_.caps().reportsAction && !v.action.empty();
        setText(actionLabel_, shown ? actionLabel(v.action) : std::string());
        lv_obj_set_style_text_color(actionLabel_, actionColor(v.action), 0);
    }

    setVisible(singleBox_, kind == SetpointKind::Single);
    setVisible(rangeBox_, kind == SetpointKind::Range);
    setVisible(noneLabel_, kind == SetpointKind::None);
    setText(noneLabel_, formatTemperature(v.current));

    const double step = controller_.caps().bounds.step;
    auto setpointText = [step](const std::optional<double>& x)
    {
        return x ? formatSetpoint(*x, step) : std::string("--");
    };
    std::string target = setpointText(v.target);
    if (degreeLabel_ && target.size() > 2 && target.compare(target.size() - 2, 2, "°") == 0)
        target.resize(target.size() - 2);
    setText(targetLabel_, target);
    setVisible(degreeLabel_, v.target.has_value());
    setText(lowLabel_, setpointText(v.low));
    setText(highLabel_, setpointText(v.high));
    if (targetLabel_)
        lv_obj_set_style_text_color(targetLabel_, editable ? accent : theme_color_grey, 0);
    if (degreeLabel_)
        lv_obj_set_style_text_color(degreeLabel_, editable ? accent : theme_color_grey, 0);
    if (lowLabel_)
        lv_obj_set_style_text_color(lowLabel_, editable ? theme_color_orange : theme_color_grey, 0);
    if (highLabel_)
        lv_obj_set_style_text_color(highLabel_, editable ? theme_color_blue : theme_color_grey, 0);

    for (lv_obj_t* b : stepButtons_)
        setEnabled(b, editable);

    // The mode is lit; fan and swing choices are only outlined, to stay secondary
    for (auto& c : modeChips_)
    {
        const bool on = c.value == v.mode;
        lv_obj_set_style_bg_color(c.obj, on ? accent : kChipBg, 0);
        lv_obj_set_style_text_color(c.label, on ? theme_color_black : theme_color_white, 0);
    }
    auto outline = [](std::vector<Chip>& chips, const std::string& selected)
    {
        for (auto& c : chips)
        {
            const bool on = c.value == selected;
            lv_obj_set_style_border_width(c.obj, on ? 2 : 0, 0);
            lv_obj_set_style_border_color(c.obj, theme_color_blue, 0);
            lv_obj_set_style_text_color(c.label, on ? theme_color_blue : theme_color_white, 0);
        }
    };
    outline(fanChips_, v.fanMode);
    outline(swingChips_, v.swingMode);

    // The row caption already names the choice
    auto cycleText = [](const std::string& option)
    {
        return option.empty() ? std::string("--") : optionLabel(option);
    };
    setText(fanCycleLabel_, cycleText(v.fanMode));
    setText(swingCycleLabel_, cycleText(v.swingMode));
}

void ClimateWidget::eventCb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (binding && binding->widget)
        binding->widget->handle(binding->op, binding->value);
}

void ClimateWidget::handle(Op op, const std::string& value)
{
    bool changed = false;
    bool now = false;

    const auto cycle = [](const std::vector<std::string>& list, const std::string& current)
    {
        auto it = std::find(list.begin(), list.end(), current);
        return (it == list.end() || it + 1 == list.end()) ? list.front() : *(it + 1);
    };

    switch (op)
    {
        case Op::TargetDown: changed = controller_.stepTarget(-1); break;
        case Op::TargetUp: changed = controller_.stepTarget(1); break;
        case Op::LowDown: changed = controller_.stepLow(-1); break;
        case Op::LowUp: changed = controller_.stepLow(1); break;
        case Op::HighDown: changed = controller_.stepHigh(-1); break;
        case Op::HighUp: changed = controller_.stepHigh(1); break;
        case Op::CycleFan:
            if (!controller_.caps().fanModes.empty())
                changed = controller_.selectFan(cycle(controller_.caps().fanModes, controller_.view().fanMode));
            break;
        case Op::CycleSwing:
            if (!controller_.caps().swingModes.empty())
                changed = controller_.selectSwing(cycle(controller_.caps().swingModes, controller_.view().swingMode));
            break;
        // A choice among visible buttons is final: no reason to wait
        case Op::Mode: changed = controller_.selectMode(value); now = true; break;
        case Op::Fan: changed = controller_.selectFan(value); now = true; break;
        case Op::Swing: changed = controller_.selectSwing(value); now = true; break;
    }

    if (!changed)
        return;

    refresh();
    if (now)
        flush();
    else
        scheduleFlush();
}

void ClimateWidget::scheduleFlush()
{
    flushTimer_->reset();
    flushTimer_->resume();
}

void ClimateWidget::flush(bool repaint)
{
    flushTimer_->pause();

    auto cmd = controller_.takeCommand();
    if (!cmd)
    {
        if (repaint)
            refresh();
        return;
    }

    const bool sent = cmd->isPlainMode() ? sendDeviceMode(cmd->mode) : sendDeviceCommand(cmd->partial);
    if (!sent)
    {
        ESP_LOGW(TAG, "Command for %s not sent, back to the server state", config.io_id.c_str());
        controller_.refused();
    }
    else
    {
        echoTimer_->reset();
        echoTimer_->resume();
    }
    if (repaint)
        refresh();
}

#include "climate_small_widget.h"
#include "../theme.h"
#include "climate_ui.h"
#include "logging.h"
#include <algorithm>

static const char* TAG = "widget.climate.small";

using namespace climate;
using namespace climate_ui;

namespace
{

constexpr int kPad = 12;

const std::vector<const lv_font_t*> kReadingFonts = {&roboto_light_48, &roboto_light_26};
const std::vector<const lv_font_t*> kDialFonts = {&roboto_bold_32, &roboto_bold_28, &roboto_bold_24};
const std::vector<const lv_font_t*> kSetpointFonts = {&roboto_regular_24, &roboto_regular_22};
const std::vector<const lv_font_t*> kStatusFonts = {&roboto_regular_24, &roboto_regular_22};

lv_obj_t* wrapped(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int lines)
{
    lv_obj_t* l = label(parent, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(l, lines * lv_font_get_line_height(font));
    return l;
}

} // namespace

ClimateSmallWidget::ClimateSmallWidget(lv_obj_t* parent,
                                       const CalaosProtocol::WidgetConfig& config,
                                       const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo),
    layout_(chooseLayout(config.w, config.h))
{
    ESP_LOGI(TAG, "Creating small climate widget: %s (size %dx%d)", config.io_id.c_str(), config.w, config.h);

    setRadius(20);
    setBorderWidth(2);
    setPadding(kPad, kPad, kPad, kPad);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(get(), theme_color_blue, when(LV_STATE_PRESSED));
    lv_obj_add_event_cb(get(), tapCb, LV_EVENT_CLICKED, this);

    contentW_ = config.w * gridInfo.cellWidth - 2 * gridInfo.padding - 2 * kPad - 4;
    contentH_ = config.h * gridInfo.cellHeight - 2 * gridInfo.padding - 2 * kPad - 4;

    secondTimer_ = std::make_unique<LvglTimer>([this]() { refresh(); }, 1000);
    secondTimer_->pause();

    reader_.read(currentState);
    clock_.observe(reader_.values().lockout, lv_tick_get());
    build();
    refresh();
}

ClimateSmallWidget::~ClimateSmallWidget()
{
    lv_async_call_cancel(closeCb, this);
    detail_.reset();
}

std::string ClimateSmallWidget::structureKey() const
{
    const Values& v = reader_.values();
    return std::to_string(static_cast<int>(setpointKind(reader_.caps(), v.mode))) +
           (reader_.present() ? "d" : "") + (reader_.caps().setpoint ? "s" : "") +
           formatSetpoint(reader_.caps().bounds.max, reader_.caps().bounds.step);
}

void ClimateSmallWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    reader_.read(state);
    clock_.observe(reader_.values().lockout, lv_tick_get());

    if (structureKey() != builtKey_)
        build();
    refresh();

    if (detail_)
    {
        detail_->setTitle(getDisplayName());
        detail_->update(reader_.caps(), reader_.values(), reader_.present());
    }
}

void ClimateSmallWidget::onCommandRefused()
{
    if (detail_)
        detail_->refused();
}

void ClimateSmallWidget::build()
{
    builtKey_ = structureKey();

    if (body_)
        lv_obj_delete(body_);
    dial_.reset();
    nameLabel_ = roomLabel_ = setLabel_ = setRow_ = statusIcon_ = statusLabel_ = gauge_ = nullptr;
    valueLabel_ = lowLabel_ = highLabel_ = nullptr;

    body_ = box(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(body_, LV_PCT(100), LV_PCT(100));

    switch (layout_)
    {
        case Layout::Compact: buildCompact(body_); break;
        case Layout::Wide: buildWide(body_); break;
        case Layout::Tall: buildTall(body_); break;
        case Layout::Large: break;
    }
}

void ClimateSmallWidget::buildStatus(lv_obj_t* parent, const lv_font_t* font)
{
    lv_obj_t* row = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    statusIcon_ = icon(row, Icon::Help, theme_color_muted);
    statusLabel_ = label(row, font, theme_color_muted);
}

void ClimateSmallWidget::buildMiniDial(lv_obj_t* parent, int diameter, int stroke, int knob)
{
    dial_ = std::make_unique<ClimateDial>(parent, diameter, stroke, knob, true, theme_color_widget_bg_off);

    const Bounds& b = reader_.caps().bounds;
    const std::string widest = formatSetpoint(b.max, b.step);
    const int inner = diameter - 4 * stroke - 2 * knob;

    lv_obj_t* in = dial_->content();
    valueLabel_ = label(in, fit(kDialFonts, widest, inner), theme_color_yellow, "--");

    const lv_font_t* rangeFont = diameter >= 190 ? &roboto_regular_28 : &roboto_regular_24;
    lowLabel_ = label(in, rangeFont, theme_color_yellow, "--");
    highLabel_ = label(in, rangeFont, theme_color_blue, "--");
}

void ClimateSmallWidget::buildCompact(lv_obj_t* body)
{
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // A 4x4 grid makes 180 px cells: the name drops to one line, then the reading shrinks
    const auto needed = [](const lv_font_t* reading, int nameLines)
    {
        return lv_font_get_line_height(reading) + 2 * lv_font_get_line_height(&roboto_regular_24) +
               nameLines * lv_font_get_line_height(&roboto_regular_22) + 6;
    };
    const lv_font_t* reading = fit(kReadingFonts, "00.0°", contentW_);
    int nameLines = 2;
    if (needed(reading, nameLines) > contentH_)
        nameLines = 1;
    if (needed(reading, nameLines) > contentH_)
        reading = &roboto_light_26;

    roomLabel_ = label(body, reading, theme_color_yellow, "--");

    setRow_ = box(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(setRow_, 6, 0);
    chevronRight(setRow_, theme_color_muted);
    setLabel_ = label(setRow_, &roboto_regular_24, theme_color_white, "--");

    buildStatus(body, &roboto_regular_24);

    nameFont_ = &roboto_regular_22;
    nameLines_ = nameLines;
    nameWidth_ = contentW_;
    nameLabel_ = wrapped(body, nameFont_, theme_color_blue, nameLines);
    lv_obj_set_width(nameLabel_, LV_PCT(100));
    lv_obj_set_style_text_align(nameLabel_, LV_TEXT_ALIGN_CENTER, 0);
}

void ClimateSmallWidget::buildWide(lv_obj_t* body)
{
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 16, 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int diameter = std::min(168, static_cast<int>(contentH_ / 0.935));
    buildMiniDial(body, diameter, 9, 10);

    const int textW = contentW_ - diameter - 16;
    lv_obj_t* col = box(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(col, textW);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // The status (two lines when it waits), the gauge and the room reading come first
    // in a 180 px cell: the name takes what is left
    const int others = 2 * lv_font_get_line_height(&roboto_regular_24) + 7 + lv_font_get_line_height(&roboto_regular_24) + 12;
    const lv_font_t* nameFont = &roboto_regular_28;
    int nameLines = 2;
    if (others + nameLines * lv_font_get_line_height(nameFont) > contentH_)
        nameFont = &roboto_regular_24;
    if (others + nameLines * lv_font_get_line_height(nameFont) > contentH_)
        nameLines = 1;
    nameFont_ = nameFont;
    nameLines_ = nameLines;
    nameWidth_ = textW;
    nameLabel_ = wrapped(col, nameFont, theme_color_blue, nameLines);
    lv_obj_set_width(nameLabel_, LV_PCT(100));
    buildStatus(col, &roboto_regular_24);
    lv_obj_set_width(statusLabel_, textW - 32);
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_WRAP);
    gauge_ = gauge(col, textW, theme_color_amber);
    roomLabel_ = label(col, &roboto_regular_24, theme_color_muted);
}

void ClimateSmallWidget::buildTall(lv_obj_t* body)
{
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int diameter = std::min(200, contentW_);
    const bool big = diameter > 180;
    buildMiniDial(body, diameter, big ? 12 : 10, big ? 13 : 11);

    lv_obj_t* col = box(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_style_pad_row(col, 10, 0);

    roomLabel_ = label(col, &roboto_regular_24, theme_color_muted);
    buildStatus(col, &roboto_regular_26);
    // Long words ("Démarre dans 3 min") break onto a second line in the narrow cell
    lv_obj_set_style_max_width(statusLabel_, contentW_ - 32, 0);
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    gauge_ = gauge(col, std::min(diameter, contentW_) * 4 / 5, theme_color_amber);
    nameFont_ = &roboto_regular_26;
    nameLines_ = 2;
    nameWidth_ = contentW_;
    nameLabel_ = wrapped(col, nameFont_, theme_color_blue, 2);
    lv_obj_set_width(nameLabel_, LV_PCT(100));
    lv_obj_set_style_text_align(nameLabel_, LV_TEXT_ALIGN_CENTER, 0);
}

void ClimateSmallWidget::refresh()
{
    const Values& v = reader_.values();
    const Capabilities& caps = reader_.caps();
    const bool present = reader_.present();
    const SetpointKind kind = present ? setpointKind(caps, v.mode) : SetpointKind::None;
    const uint32_t now = lv_tick_get();
    const Status st = statusOf(caps, v, present, clock_.remaining(now));

    const Surface surface = surfaceFor(st.backdrop);
    const lv_color_t bg = surface.bg;
    const lv_color_t border = surface.border;
    setBgColor(bg);
    setBorderColor(border);
    lv_obj_set_style_border_color(get(), border, 0);
    if (present)
        lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_remove_flag(get(), LV_OBJ_FLAG_CLICKABLE);

    setText(nameLabel_, getDisplayName());
    fitName();

    const bool wide = layout_ != Layout::Compact;
    std::string statusText = wide ? st.widgetText : st.shortText;
    if (layout_ == Layout::Tall && lineCount(statusText, &roboto_regular_26, contentW_ - 32) > 2)
        statusText = st.shortText;
    if (layout_ == Layout::Compact)
    {
        // A long word takes the smaller font, then gives way to dots, but never leaves the cell
        const int room = contentW_ - 32;
        const lv_font_t* f = fit(kStatusFonts, statusText, room);
        lv_obj_set_style_text_font(statusLabel_, f, 0);
        lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_DOT);
        lv_obj_set_width(statusLabel_, std::min(room, static_cast<int>(textWidth(statusText.c_str(), f)) + 2));
        lv_obj_set_height(statusLabel_, lv_font_get_line_height(f));
    }
    setText(statusLabel_, statusText);
    lv_obj_set_style_text_color(statusLabel_, toneColor(st.tone), 0);
    setIcon(statusIcon_, st.icon, toneColor(st.tone));

    const bool waiting = st.phase == Phase::Waiting;
    setVisible(gauge_, waiting);
    if (waiting)
        setGauge(gauge_, clock_.elapsedFraction(now));
    if (waiting)
        secondTimer_->resume();
    else
        secondTimer_->pause();

    const std::string room = formatReading(v.current);
    if (layout_ == Layout::Compact)
    {
        setText(roomLabel_, room);
        const std::string set = setpointText(kind, v, caps.bounds);
        setVisible(setRow_, kind != SetpointKind::None);
        setText(setLabel_, set);
        lv_obj_set_style_text_font(setLabel_, fit(kSetpointFonts, set, contentW_ - 24), 0);
    }
    else
        setText(roomLabel_, "Pièce " + room);

    if (dial_)
    {
        dial_->setSeparator(bg);
        dial_->set(dialFor(caps, kind, v, st));

        const bool live = st.canSet && st.phase != Phase::Off;
        setVisible(valueLabel_, kind != SetpointKind::Range);
        setVisible(lowLabel_, kind == SetpointKind::Range);
        setVisible(highLabel_, kind == SetpointKind::Range);
        if (kind == SetpointKind::Range)
        {
            const auto shown = [&](const std::optional<double>& x)
            {
                return x ? formatShort(*x, caps.bounds.step) : std::string("--");
            };
            setText(lowLabel_, shown(v.low));
            setText(highLabel_, shown(v.high));
            lv_obj_set_style_text_color(lowLabel_, live ? theme_color_yellow : theme_color_muted, 0);
            lv_obj_set_style_text_color(highLabel_, live ? theme_color_blue : theme_color_muted, 0);
        }
        else
        {
            // Without a setpoint the dial carries the room temperature
            const bool none = kind == SetpointKind::None;
            setText(valueLabel_, none ? room : setpointText(kind, v, caps.bounds));
            lv_obj_set_style_text_color(valueLabel_, none ? theme_color_white : (live ? theme_color_yellow : theme_color_muted), 0);
        }
    }
}

void ClimateSmallWidget::fitName()
{
    if (!nameLabel_)
        return;
    // A name that does not fit its lines scrolls on one, like the other widgets' names
    const bool fits = lineCount(getDisplayName(), nameFont_, nameWidth_) <= nameLines_;
    lv_label_set_long_mode(nameLabel_, fits ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_height(nameLabel_, (fits ? nameLines_ : 1) * lv_font_get_line_height(nameFont_));
}

void ClimateSmallWidget::tapCb(lv_event_t* e)
{
    auto* self = static_cast<ClimateSmallWidget*>(lv_event_get_user_data(e));
    if (self)
        self->open();
}

void ClimateSmallWidget::open()
{
    if (detail_ || !reader_.present())
        return;

    ClimateDetail::Host host;
    host.send = [this](const Command& cmd)
    {
        return cmd.isPlainMode() ? sendDeviceMode(cmd.mode) : sendDeviceCommand(cmd.partial);
    };
    host.closed = [this]() { detailClosed(); };

    detail_ = std::make_unique<ClimateDetail>(getDisplayName(), host);
    detail_->update(reader_.caps(), reader_.values(), reader_.present());
}

void ClimateSmallWidget::detailClosed()
{
    // The close button is part of the view: it cannot be destroyed under its own click
    lv_async_call(closeCb, this);
}

void ClimateSmallWidget::closeCb(void* self)
{
    static_cast<ClimateSmallWidget*>(self)->detail_.reset();
}

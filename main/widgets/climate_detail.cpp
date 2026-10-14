#include "climate_detail.h"
#include "../theme.h"
#include "climate_ui.h"
#include "logging.h"
#include <algorithm>

static const char* TAG = "widget.climate.detail";

using namespace climate;
using namespace climate_ui;

namespace
{

// NotificationToast's full-screen background
const lv_color_t kScreenBg = LV_COLOR_MAKE(0x14, 0x14, 0x14);

std::string join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v)
        out += s + ",";
    return out;
}

} // namespace

ClimateDetail::ClimateDetail(const std::string& title, Host host):
    host_(std::move(host)),
    title_(title)
{
    width_ = screenW_ = lv_display_get_horizontal_resolution(lv_display_get_default());
    height_ = screenH_ = lv_display_get_vertical_resolution(lv_display_get_default());
    init();

    // Under the toast, the OTA screen and the screensaver, which must stay on top
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay_);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(overlay_, width_, height_);
    lv_obj_set_style_bg_color(overlay_, kScreenBg, 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_move_to_index(overlay_, 0);
}

ClimateDetail::ClimateDetail(lv_obj_t* parent, int width, int height, const std::string& title, Host host):
    host_(std::move(host)),
    title_(title),
    embedded_(true),
    parent_(parent)
{
    width_ = width;
    height_ = height;
    screenW_ = lv_display_get_horizontal_resolution(lv_display_get_default());
    screenH_ = lv_display_get_vertical_resolution(lv_display_get_default());
    init();
}

void ClimateDetail::init()
{
    // Created paused: resumed by a tap or a wait, paused again when fired
    flushTimer_ = std::make_unique<LvglTimer>([this]() { flush(); }, kCoalesceMs);
    flushTimer_->pause();
    echoTimer_ = std::make_unique<LvglTimer>([this]()
    {
        echoTimer_->pause();
        controller_.expireSent();
        refresh();
    }, kEchoTimeoutMs);
    echoTimer_->pause();
    secondTimer_ = std::make_unique<LvglTimer>([this]() { refresh(); }, 1000);
    secondTimer_->pause();
}

ClimateDetail::~ClimateDetail()
{
    // A page rebuilt mid-edit must not lose the user's last taps
    if (controller_.hasPending())
        flush(false);
    if (overlay_)
        lv_obj_delete(overlay_);
    else
    {
        // Embedded: the widget goes on without us, the popup lives on the top layer
        if (veil_)
            lv_obj_delete(veil_);
        if (body_)
            lv_obj_delete(body_);
    }
}

DetailMetrics ClimateDetail::metrics() const
{
    return embedded_ ? widgetMetrics(width_, height_, controller_.kind())
                     : detailMetrics(width_, height_, controller_.kind());
}

lv_obj_t* ClimateDetail::popupParent() const
{
    return overlay_ ? overlay_ : lv_layer_top();
}

uint32_t ClimateDetail::now() const
{
    return lv_tick_get();
}

void ClimateDetail::setTitle(const std::string& title)
{
    title_ = title;
    setText(title_label_, title_);
}

void ClimateDetail::update(const Capabilities& caps, const Values& values, bool hasDevice)
{
    hasDevice_ = hasDevice;
    controller_.update(hasDevice ? caps : Capabilities{}, hasDevice ? values : Values{});
    clock_.observe(hasDevice ? values.lockout : std::nullopt, now());
    refresh();
}

void ClimateDetail::refused()
{
    ESP_LOGW(TAG, "Command refused, back to the server state");
    flushTimer_->pause();
    echoTimer_->pause();
    controller_.refused();
    refresh();
}

std::string ClimateDetail::structureKey() const
{
    const Capabilities& c = controller_.caps();
    const Values v = controller_.view();
    const bool more = !c.fanModes.empty() || !c.swingModes.empty() || v.humidity.has_value();
    return std::to_string(static_cast<int>(controller_.kind())) + (more ? "m" : "") +
           (hasDevice_ ? "d" : "");
}

std::string ClimateDetail::popupKey() const
{
    const Capabilities& c = controller_.caps();
    return join(c.modes) + "|" + join(c.fanModes) + "|" + join(c.swingModes) + "|" +
           (controller_.view().humidity ? "h" : "");
}

lv_obj_t* ClimateDetail::addControl(lv_obj_t* obj, Op op, const std::string& value, bool inPopup)
{
    // The popup outlives a rebuilt body: its bindings must not go with the body's
    auto& bindings = inPopup ? *popupBindings_ : bindings_;
    bindings.push_back({this, op, value});
    lv_obj_add_event_cb(obj, eventCb, LV_EVENT_CLICKED, &bindings.back());
    (inPopup ? popupControls_ : controls_).push_back({op, value, obj});
    return obj;
}

void ClimateDetail::buildBody()
{
    builtKey_ = structureKey();

    if (body_)
        lv_obj_delete(body_);
    dial_.reset();
    bindings_.clear();
    controls_.clear();
    title_label_ = roomLabel_ = wordLabel_ = gauge_ = stopLabel_ = nullptr;
    numberLabel_ = degreeLabel_ = lowLabel_ = highLabel_ = nullptr;
    pill_ = pillIcon_ = pillLabel_ = more_ = nullptr;

    const DetailMetrics m = metrics();

    body_ = lv_obj_create(embedded_ ? parent_ : overlay_);
    lv_obj_remove_style_all(body_);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(body_, width_, height_);
    lv_obj_set_style_pad_all(body_, m.margin, 0);
    if (!embedded_)
        lv_obj_move_to_index(body_, 0);

    const Capabilities& c = controller_.caps();
    const bool hasMore = !c.fanModes.empty() || !c.swingModes.empty() || controller_.view().humidity.has_value();

    if (embedded_)
    {
        // Mode and settings on top, the title at the bottom like the other widgets
        lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* top = box(body_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(top, 12, 0);
        buildPill(top);
        if (hasMore)
            buildMore(top);
        buildDial(body_);
        buildLine(body_);

        title_label_ = label(body_, &roboto_regular_24, theme_color_blue, title_.c_str());
        lv_obj_set_width(title_label_, LV_PCT(100));
        lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    else if (m.landscape)
    {
        lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(body_, 24, 0);
        lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* left = box(body_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(left, m.columnWidth, LV_PCT(100));
        lv_obj_set_style_pad_row(left, 10, 0);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        buildDial(left);
        buildLine(left);

        lv_obj_t* right = box(body_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_height(right, LV_PCT(100));
        lv_obj_set_flex_grow(right, 1);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        buildHeader(right);

        lv_obj_t* spacer = box(right, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(spacer, 1, 0);
        lv_obj_set_flex_grow(spacer, 1);
        buildPill(right);
        if (hasMore)
            buildMore(right);
    }
    else
    {
        lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(body_, 10, 0);

        buildHeader(body_);
        buildDial(body_);
        buildLine(body_);
        buildPill(body_);

        lv_obj_t* spacer = box(body_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(spacer, 1, 0);
        lv_obj_set_flex_grow(spacer, 1);
        if (hasMore)
            buildMore(body_);
    }
}

void ClimateDetail::buildHeader(lv_obj_t* parent)
{
    lv_obj_t* head = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_style_pad_column(head, 12, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title_label_ = label(head, &roboto_regular_32, theme_color_blue, title_.c_str());
    lv_obj_set_flex_grow(title_label_, 1);
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_WRAP);

    lv_obj_t* close = stepButton(head, false, 64);
    lv_obj_clean(close);
    lv_obj_center(cross(close, theme_color_white));
    addControl(close, Op::Close);
}

void ClimateDetail::buildDial(lv_obj_t* parent)
{
    const DetailMetrics m = metrics();
    dial_ = std::make_unique<ClimateDial>(parent, m.dial, m.stroke, m.knob, false,
                                          embedded_ ? theme_color_widget_bg_off : kScreenBg);

    lv_obj_t* in = dial_->content();
    roomLabel_ = label(in, &roboto_light_26, theme_color_white);
    wordLabel_ = label(in, &roboto_regular_24, theme_color_muted);
    // "Démarre dans 2 min 20" is wider than a small dial: it breaks in two
    lv_obj_set_style_max_width(wordLabel_, dial_->innerWidth(), 0);
    lv_label_set_long_mode(wordLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(wordLabel_, LV_TEXT_ALIGN_CENTER, 0);
    gauge_ = gauge(in, std::min(160, dial_->innerWidth()), theme_color_amber);
    lv_obj_set_style_margin_top(gauge_, 8, 0);

    // A widget has no room under the line for the note: it goes inside the dial
    if (embedded_ && controller_.kind() == SetpointKind::Range)
        stopLabel_ = label(in, &roboto_regular_22, theme_color_muted);
}

lv_obj_t* ClimateDetail::numberGroup(lv_obj_t* parent, Op minus, Op plus, lv_obj_t** value,
                                     lv_color_t color, bool big)
{
    const DetailMetrics m = metrics();
    lv_obj_t* group = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(group, 8, 0);

    addControl(stepButton(group, false, m.button), minus);

    lv_obj_t* number = box(group, LV_FLEX_FLOW_ROW);
    const bool lettered = big && m.numberPx == 80;
    lv_obj_set_style_min_width(number, lettered ? 190 : (big ? 150 : 110), 0);
    lv_obj_set_flex_align(number, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    *value = label(number, lettered ? &roboto_regular_80 : (big ? &roboto_regular_60 : &roboto_regular_48), color, "--");
    if (lettered)
    {
        // The 80 px font has no degree sign: it is lettered apart, small and raised
        degreeLabel_ = label(number, &roboto_regular_48, color, "°");
        lv_obj_set_style_pad_top(degreeLabel_, 6, 0);
    }

    addControl(stepButton(group, true, m.button), plus);
    return group;
}

void ClimateDetail::buildLine(lv_obj_t* parent)
{
    const SetpointKind kind = controller_.kind();
    const DetailMetrics m = metrics();

    if (kind == SetpointKind::Single)
        numberGroup(parent, Op::Minus, Op::Plus, &numberLabel_, theme_color_yellow, true);
    else if (kind == SetpointKind::Range)
    {
        lv_obj_t* line = box(parent, m.stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_row(line, 4, 0);
        lv_obj_set_style_pad_column(line, 24, 0);
        numberGroup(line, Op::LowMinus, Op::LowPlus, &lowLabel_, theme_color_yellow, false);
        numberGroup(line, Op::HighMinus, Op::HighPlus, &highLabel_, theme_color_blue, false);
        if (!embedded_)
        {
            stopLabel_ = label(parent, &roboto_regular_22, theme_color_muted);
            lv_obj_set_style_margin_top(stopLabel_, 8, 0);
        }
    }
}

void ClimateDetail::buildPill(lv_obj_t* parent)
{
    pill_ = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(pill_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(pill_, metrics().pill);
    lv_obj_set_style_pad_hor(pill_, embedded_ ? 18 : 22, 0);
    lv_obj_set_style_pad_column(pill_, 12, 0);
    lv_obj_set_style_radius(pill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill_, theme_color_widget_bg_off, 0);
    lv_obj_set_style_bg_opa(pill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill_, theme_color_widget_border_off, 0);
    lv_obj_set_style_border_width(pill_, 2, 0);
    lv_obj_set_style_border_color(pill_, theme_color_blue, when(LV_STATE_PRESSED));
    lv_obj_set_style_opa(pill_, LV_OPA_50, when(LV_STATE_DISABLED));
    if (!embedded_)
        lv_obj_set_style_margin_top(pill_, 10, 0);

    pillIcon_ = icon(pill_, Icon::Help, theme_color_white);
    pillLabel_ = label(pill_, &roboto_regular_24, theme_color_white);
    chevronDown(pill_, theme_color_muted);
    addControl(pill_, Op::OpenMode);
}

void ClimateDetail::buildMore(lv_obj_t* parent)
{
    more_ = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(more_, LV_OBJ_FLAG_CLICKABLE);
    if (embedded_)
    {
        // A square button with three dots, like the - and + buttons
        const int size = metrics().pill;
        lv_obj_set_size(more_, size, size);
        lv_obj_set_style_radius(more_, 16, 0);
        lv_obj_set_style_bg_color(more_, theme_color_widget_bg_off, 0);
        lv_obj_set_style_bg_opa(more_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(more_, theme_color_widget_border_off, 0);
        lv_obj_set_style_border_width(more_, 2, 0);
        lv_obj_set_style_border_color(more_, theme_color_blue, when(LV_STATE_PRESSED));
        dots(more_, theme_color_muted);
    }
    else
    {
        lv_obj_set_height(more_, 56);
        lv_obj_set_style_pad_column(more_, 10, 0);
        lv_obj_set_style_pad_hor(more_, 16, 0);
        lv_obj_set_style_margin_top(more_, 14, 0);
        label(more_, &roboto_regular_24, theme_color_muted, "Plus de réglages");
        chevronRight(more_, theme_color_muted);
    }
    addControl(more_, Op::OpenMore);
}

void ClimateDetail::refresh()
{
    if (structureKey() != builtKey_ || !body_)
        buildBody();

    const Values v = controller_.view();
    const Capabilities& caps = controller_.caps();
    const SetpointKind kind = controller_.kind();
    const uint32_t t = now();
    const double remaining = clock_.remaining(t);
    const Status st = statusOf(caps, v, hasDevice_, remaining);
    const bool editable = controller_.canEditSetpoint() && st.canSet;

    dial_->set(dialFor(caps, kind, v, st));
    if (embedded_)
    {
        // The widget is lit by what the device does; the dial's knobs ring follows
        dial_->setSeparator(surfaceFor(st.backdrop).bg);
        if (host_.styled)
            host_.styled(st);
    }

    setText(roomLabel_, "Pièce " + formatReading(v.current));
    setText(wordLabel_, st.text);
    lv_obj_set_style_text_color(wordLabel_, toneColor(st.tone), 0);
    const bool waiting = st.phase == Phase::Waiting;
    setVisible(gauge_, waiting);
    if (waiting)
        setGauge(gauge_, clock_.elapsedFraction(t));

    const lv_color_t single = editable ? theme_color_yellow : theme_color_muted;
    if (numberLabel_)
    {
        std::string text = setpointText(SetpointKind::Single, v, caps.bounds);
        const bool known = v.target.has_value();
        // Only the 80 px number has its degree lettered apart
        if (known && degreeLabel_)
            text.resize(text.size() - std::string("°").size());
        setText(numberLabel_, text);
        lv_obj_set_style_text_color(numberLabel_, single, 0);
        if (degreeLabel_)
        {
            setVisible(degreeLabel_, known);
            lv_obj_set_style_text_color(degreeLabel_, single, 0);
        }
    }
    if (lowLabel_ && highLabel_)
    {
        const auto shown = [&](const std::optional<double>& x)
        {
            return x ? formatShort(*x, caps.bounds.step) : std::string("--");
        };
        setText(lowLabel_, shown(v.low));
        setText(highLabel_, shown(v.high));
        lv_obj_set_style_text_color(lowLabel_, editable ? theme_color_yellow : theme_color_muted, 0);
        lv_obj_set_style_text_color(highLabel_, editable ? theme_color_blue : theme_color_muted, 0);
    }
    if (stopLabel_)
    {
        const bool stop = atMinRange(caps, kind, v);
        setText(stopLabel_, stop ? minRangeText(caps.bounds, embedded_) : std::string());
        setVisible(stopLabel_, stop);
    }

    canSet_ = st.canSet;
    const Steppers s = st.canSet ? steppers(controller_) : Steppers{};
    for (const auto& c : controls_)
    {
        switch (c.op)
        {
            case Op::Minus: setEnabled(c.obj, s.minus); break;
            case Op::Plus: setEnabled(c.obj, s.plus); break;
            case Op::LowMinus: setEnabled(c.obj, s.lowMinus); break;
            case Op::LowPlus: setEnabled(c.obj, s.lowPlus); break;
            case Op::HighMinus: setEnabled(c.obj, s.highMinus); break;
            case Op::HighPlus: setEnabled(c.obj, s.highPlus); break;
            default: break;
        }
    }

    setIcon(pillIcon_, modeIcon(v.mode), theme_color_white);
    setText(pillLabel_, embedded_ ? modeShortFr(v.mode) : modeLabelFr(v.mode));
    setEnabled(pill_, st.phase != Phase::Offline && !caps.modes.empty());

    if (waiting)
        secondTimer_->resume();
    else
        secondTimer_->pause();

    refreshPopup();
}

void ClimateDetail::eventCb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (!binding || !binding->view)
        return;
    // handle() may close the popup, which frees the binding it was reached through
    const Binding copy = *binding;
    copy.view->handle(copy.op, copy.value);
}

void ClimateDetail::handle(Op op, const std::string& value)
{
    bool changed = false;
    bool atOnce = false;

    // An unreachable device cannot be set: the buttons are greyed, and a stray tap is ignored too
    const bool stepping = op == Op::Minus || op == Op::Plus || op == Op::LowMinus || op == Op::LowPlus ||
                          op == Op::HighMinus || op == Op::HighPlus;
    if (stepping && !canSet_)
        return;

    switch (op)
    {
        case Op::Minus: changed = controller_.stepTarget(-1); break;
        case Op::Plus: changed = controller_.stepTarget(1); break;
        case Op::LowMinus: changed = controller_.stepLow(-1); break;
        case Op::LowPlus: changed = controller_.stepLow(1); break;
        case Op::HighMinus: changed = controller_.stepHigh(-1); break;
        case Op::HighPlus: changed = controller_.stepHigh(1); break;
        case Op::OpenMode: openPopup(Popup::Mode); return;
        case Op::OpenMore: openPopup(Popup::More); return;
        case Op::ClosePopup: closePopup(); return;
        case Op::Close:
            flushTimer_->pause();
            flush(false);
            lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            if (host_.closed)
                host_.closed();
            return;
        // A choice among visible buttons is final: no reason to wait
        case Op::Mode:
            changed = controller_.selectMode(value);
            atOnce = true;
            closePopup();
            break;
        case Op::Fan: changed = controller_.selectFan(value); atOnce = true; break;
        case Op::Swing: changed = controller_.selectSwing(value); atOnce = true; break;
    }

    if (!changed)
        return;

    refresh();
    if (atOnce)
        flush();
    else
        scheduleFlush();
}

void ClimateDetail::scheduleFlush()
{
    flushTimer_->reset();
    flushTimer_->resume();
}

void ClimateDetail::flush(bool repaint)
{
    flushTimer_->pause();

    auto cmd = controller_.takeCommand();
    if (!cmd)
    {
        if (repaint)
            refresh();
        return;
    }

    if (!host_.send || !host_.send(*cmd))
    {
        ESP_LOGW(TAG, "Command not sent, back to the server state");
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

void ClimateDetail::openPopup(Popup which)
{
    if (popup_ != Popup::None)
        closePopup();
    popup_ = which;
    buildPopup(which);
}

void ClimateDetail::closePopup()
{
    popup_ = Popup::None;
    modeChoices_.clear();
    fanChoices_.clear();
    swingChoices_.clear();
    humidityLabel_ = nullptr;
    popupBuiltKey_.clear();
    if (veil_)
    {
        // Often called from the click of one of its own buttons
        lv_obj_add_flag(veil_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(veil_);
        veil_ = nullptr;
    }
    // The bindings belong to the veil and go with it, when it is really deleted:
    // a button waiting for that can still be sent a click
    popupBindings_ = nullptr;
    popupControls_.clear();
}

void ClimateDetail::buildPopup(Popup which)
{
    const Capabilities& caps = controller_.caps();
    const Values v = controller_.view();
    popupBuiltKey_ = popupKey();

    veil_ = lv_obj_create(popupParent());
    popupBindings_ = new std::deque<Binding>();
    lv_obj_add_event_cb(veil_, [](lv_event_t* e)
    {
        delete static_cast<std::deque<Binding>*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, popupBindings_);
    // Over the widget, still under what must always be on top
    if (embedded_)
        lv_obj_move_to_index(veil_, 0);
    lv_obj_remove_style_all(veil_);
    lv_obj_remove_flag(veil_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(veil_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(veil_, screenW_, screenH_);
    lv_obj_set_style_bg_color(veil_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(veil_, LV_OPA_60, 0);
    lv_obj_set_flex_flow(veil_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(veil_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addControl(veil_, Op::ClosePopup, "", true);

    // A short screen takes four choices to a row, and the humidity beside the title
    const bool compact = screenH_ < 600;
    const int boxWidth = std::min(compact && which == Popup::More ? 720 : 560, screenW_ - 48);
    lv_obj_t* panel = lv_obj_create(veil_);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    // Swallows the taps that are not on a choice: they would close the popup
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(panel, boxWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, 24, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_bg_color(panel, theme_color_widget_bg_off, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, theme_color_widget_border_off, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* head = box(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_bottom(head, compact ? 8 : 16, 0);
    label(head, &roboto_regular_28, theme_color_white, which == Popup::Mode ? "Mode" : "Plus de réglages");
    if (which == Popup::More && compact && v.humidity)
        humidityLabel_ = label(head, &roboto_regular_22, theme_color_muted);

    lv_obj_t* body = box(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    const int inner = boxWidth - 48 - 4;
    std::vector<lv_obj_t*> choices;

    const auto grid = [&](int columns)
    {
        lv_obj_t* g = box(body, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_width(g, LV_PCT(100));
        lv_obj_set_style_pad_row(g, 12, 0);
        lv_obj_set_style_pad_column(g, 12, 0);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return std::make_pair(g, (inner - (columns - 1) * 12) / columns);
    };

    const auto choice = [&](lv_obj_t* parent, int width, const std::string& text, Op op, const std::string& value,
                            const Icon* ic)
    {
        lv_obj_t* b = choiceButton(parent, 72);
        lv_obj_set_width(b, width);
        if (ic)
            icon(b, *ic, theme_color_white);
        label(b, &roboto_regular_24, theme_color_white, text.c_str());
        addControl(b, op, value, true);
        choices.push_back(b);
        return b;
    };

    if (which == Popup::Mode)
    {
        auto [g, w] = grid(2);
        for (const auto& m : caps.modes)
        {
            const Icon ic = modeIcon(m);
            modeChoices_.push_back({m, choice(g, w, modeLabelFr(m), Op::Mode, m, &ic)});
        }
    }
    else
    {
        bool firstSection = true;
        const auto section = [&](const char* caption, const std::vector<std::string>& options,
                                 std::string (*name)(const std::string&), Op op,
                                 std::vector<std::pair<std::string, lv_obj_t*>>* out)
        {
            if (options.empty())
                return;
            lv_obj_t* cap = label(body, &roboto_regular_22, theme_color_muted, caption);
            lv_obj_set_style_pad_top(cap, firstSection ? 0 : 16, 0);
            firstSection = false;
            lv_obj_set_style_pad_bottom(cap, 8, 0);
            auto [g, w] = grid(compact ? 4 : 3);
            for (const auto& o : options)
                out->push_back({o, choice(g, w, name(o), op, o, nullptr)});
        };
        section("Ventilation", caps.fanModes, fanLabelFr, Op::Fan, &fanChoices_);
        section("Oscillation", caps.swingModes, swingLabelFr, Op::Swing, &swingChoices_);

        if (v.humidity && !humidityLabel_)
        {
            humidityLabel_ = label(body, &roboto_regular_22, theme_color_muted);
            lv_obj_set_style_pad_top(humidityLabel_, 16, 0);
        }
    }

    lv_obj_t* close = choiceButton(panel, compact ? 56 : 64);
    lv_obj_set_width(close, LV_PCT(100));
    lv_obj_set_style_margin_top(close, compact ? 12 : 20, 0);
    label(close, &roboto_regular_24, theme_color_white, "Fermer");
    addControl(close, Op::ClosePopup, "close", true);

    fitPopup(panel, body, choices, screenH_ - 48);
    refreshPopup();
}

void ClimateDetail::fitPopup(lv_obj_t* panel, lv_obj_t* body, const std::vector<lv_obj_t*>& choices,
                             int maxHeight)
{
    // Squeeze the choices down to a finger's width, then scroll what is left
    for (int h : {72, 64, 56, 48})
    {
        for (lv_obj_t* c : choices)
            lv_obj_set_height(c, h);
        lv_obj_update_layout(panel);
        if (lv_obj_get_height(panel) <= maxHeight)
            return;
    }

    lv_obj_set_height(panel, maxHeight);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_update_layout(panel);
}

void ClimateDetail::refreshPopup()
{
    if (popup_ == Popup::None)
        return;

    // The lists changed under the popup: build it again
    if (popupKey() != popupBuiltKey_)
    {
        const Popup which = popup_;
        closePopup();
        popup_ = which;
        buildPopup(which);
        return;
    }

    const Values v = controller_.view();
    for (auto& [mode, obj] : modeChoices_)
        setChoiceActive(obj, mode == v.mode);
    for (auto& [fan, obj] : fanChoices_)
        setChoiceActive(obj, fan == v.fanMode);
    for (auto& [swing, obj] : swingChoices_)
        setChoiceActive(obj, swing == v.swingMode);

    if (humidityLabel_ && v.humidity)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Humidité %d %%", static_cast<int>(std::lround(*v.humidity)));
        setText(humidityLabel_, buf);
    }
}

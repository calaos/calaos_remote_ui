#pragma once

#include "../lvgl_timer.h"
#include "climate_dial.h"
#include "climate_logic.h"
#include "climate_present.h"
#include "lvgl.h"
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * Full-screen view of one climate device, opened by a tap on a small widget:
 * the dial, the "- 22° +" line, the mode pill and "Plus de réglages". The mode
 * and the secondary settings are popups over it.
 *
 * Gestures only edit a draft (climate::Controller); a pause after the last tap
 * sends one command, a choice in a popup sends at once.
 */
class ClimateDetail
{
public:
    struct Host
    {
        // false: the command could not leave, the draft goes back to the server value
        std::function<bool(const climate::Command&)> send;
        // The user closed the view. It must not be destroyed from inside this call.
        std::function<void()> closed;
        // Embedded in a widget: what the device does, to light the widget
        std::function<void(const climate::Status&)> styled;
    };

    // A pause this long after the last tap sends the command
    static constexpr uint32_t kCoalesceMs = 700;
    // A sent value not echoed by then gives way to the server's
    static constexpr uint32_t kEchoTimeoutMs = 4000;

    // The full-screen view, on the top layer
    ClimateDetail(const std::string& title, Host host);
    // The same content laid out inside a widget (2x2 and larger) of the given size:
    // mode and settings on top, dial, line, title at the bottom. No close button,
    // no overlay; the popups still open on the top layer.
    ClimateDetail(lv_obj_t* parent, int width, int height, const std::string& title, Host host);
    ~ClimateDetail();

    void update(const climate::Capabilities& caps, const climate::Values& values, bool hasDevice);
    void setTitle(const std::string& title);
    void refused();

private:
    enum class Op
    {
        Minus, Plus, LowMinus, LowPlus, HighMinus, HighPlus,
        OpenMode, OpenMore, Close,
        Mode, Fan, Swing, ClosePopup
    };
    enum class Popup { None, Mode, More };

    struct Binding
    {
        ClimateDetail* view;
        Op op;
        std::string value;
    };

    struct Control
    {
        Op op;
        std::string value;
        lv_obj_t* obj;
    };

    void init();
    climate::DetailMetrics metrics() const;
    lv_obj_t* popupParent() const;
    void buildBody();
    void buildHeader(lv_obj_t* parent);
    void buildDial(lv_obj_t* parent);
    void buildLine(lv_obj_t* parent);
    void buildPill(lv_obj_t* parent);
    void buildMore(lv_obj_t* parent);
    lv_obj_t* numberGroup(lv_obj_t* parent, Op minus, Op plus, lv_obj_t** value, lv_color_t color,
                          bool big);
    lv_obj_t* addControl(lv_obj_t* obj, Op op, const std::string& value = "", bool inPopup = false);

    void refresh();
    void refreshPopup();
    std::string structureKey() const;
    std::string popupKey() const;

    void openPopup(Popup which);
    void closePopup();
    void buildPopup(Popup which);
    void fitPopup(lv_obj_t* box, lv_obj_t* body, const std::vector<lv_obj_t*>& choices, int maxHeight);

    static void eventCb(lv_event_t* e);
    void handle(Op op, const std::string& value);
    void scheduleFlush();
    void flush(bool repaint = true);
    uint32_t now() const;

    Host host_;
    std::string title_;
    climate::Controller controller_;
    climate::LockoutClock clock_;
    bool hasDevice_ = false;
    bool canSet_ = false;
    // The area the content lays out in; the screen's size for the popups
    int width_ = 0;
    int height_ = 0;
    int screenW_ = 0;
    int screenH_ = 0;
    bool embedded_ = false;
    lv_obj_t* parent_ = nullptr;

    std::deque<Binding> bindings_;
    std::vector<Control> controls_;
    std::deque<Binding> popupBindings_;
    std::vector<Control> popupControls_;
    std::unique_ptr<LvglTimer> flushTimer_;
    std::unique_ptr<LvglTimer> echoTimer_;
    std::unique_ptr<LvglTimer> secondTimer_;

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* body_ = nullptr;
    std::unique_ptr<ClimateDial> dial_;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* roomLabel_ = nullptr;
    lv_obj_t* wordLabel_ = nullptr;
    lv_obj_t* gauge_ = nullptr;
    lv_obj_t* stopLabel_ = nullptr;
    lv_obj_t* numberLabel_ = nullptr;
    lv_obj_t* degreeLabel_ = nullptr;
    lv_obj_t* lowLabel_ = nullptr;
    lv_obj_t* highLabel_ = nullptr;
    lv_obj_t* pill_ = nullptr;
    lv_obj_t* pillIcon_ = nullptr;
    lv_obj_t* pillLabel_ = nullptr;
    lv_obj_t* more_ = nullptr;
    std::string builtKey_;

    Popup popup_ = Popup::None;
    std::string popupBuiltKey_;
    lv_obj_t* veil_ = nullptr;
    lv_obj_t* humidityLabel_ = nullptr;
    std::vector<std::pair<std::string, lv_obj_t*>> modeChoices_;
    std::vector<std::pair<std::string, lv_obj_t*>> fanChoices_;
    std::vector<std::pair<std::string, lv_obj_t*>> swingChoices_;

    friend struct ClimateDetailTestAccess;
};

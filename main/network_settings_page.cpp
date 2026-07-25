#include "network_settings_page.h"
#include "theme.h"
#include "app_main.h"
#include "logging.h"
#include "board_config.h"   // BOARD_HAS_WIFI / BOARD_HAS_ETHERNET
#include "../hal/hal.h"
#include "../hal/calaos_config/device_config.h"

#include <algorithm>
#include <cctype>

static const char *TAG = "NetworkSettingsPage";
extern AppMain *g_appMain;

// Neutral surface tone for input backgrounds (one step above the card bg,
// consistent with the grey scale already used by AboutPage)
static const lv_color_t color_input_bg = LV_COLOR_MAKE(0x24, 0x24, 0x24);
static const lv_color_t color_label_dim = LV_COLOR_MAKE(0x88, 0x88, 0x88);
static const lv_color_t color_placeholder = LV_COLOR_MAKE(0x5A, 0x5A, 0x5A);

// ── Small helpers ───────────────────────────────────────────────────────────

// C++20-safe part|state style selector (avoids deprecated enum-enum bitwise op)
static constexpr lv_style_selector_t sel(lv_part_t part, lv_state_t state)
{
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

static std::string trimmed(const char *text)
{
    std::string s = text ? text : "";
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// ── Construction ────────────────────────────────────────────────────────────

NetworkSettingsPage::NetworkSettingsPage(lv_obj_t *parent):
    PageBase(parent)
{
    ESP_LOGI(TAG, "Creating NetworkSettingsPage");

    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    // Width-capped centered form column: min(90% of screen, 640px)
    int32_t screenW = lv_display_get_horizontal_resolution(lv_display_get_default());
    formWidth_ = std::min<int32_t>((screenW * 90) / 100, 640);

    createUI();

    // Pre-fill from the current device config
    auto &devCfg = DeviceConfig::getInstance();
    {
        std::string curIf = devCfg.isWifi() ? "wifi" : "ethernet";
        for (size_t i = 0; i < interfaceIds_.size(); i++)
            if (interfaceIds_[i] == curIf) { lv_dropdown_set_selected(ddInterface, i); break; }
    }
    lv_textarea_set_text(taSsid, devCfg.getWifiSsid().c_str());
    lv_textarea_set_text(taPassword, devCfg.getWifiPassword().c_str());
    // Switch checked = DHCP (default); unchecked = static IP.
    if (devCfg.isDhcp())
        lv_obj_add_state(swDhcp, LV_STATE_CHECKED);
    lv_textarea_set_text(taStaticIp, devCfg.getStaticIp().c_str());
    lv_textarea_set_text(taStaticMask, devCfg.getStaticMask().c_str());
    lv_textarea_set_text(taStaticGw, devCfg.getStaticGateway().c_str());
    lv_textarea_set_text(taStaticDns, devCfg.getStaticDns().c_str());
    lv_textarea_set_text(taServerHost, devCfg.getServerHost().c_str());
    lv_textarea_set_text(taServerPort, std::to_string(devCfg.getServerPort()).c_str());
    if (devCfg.getServerSsl())
        lv_obj_add_state(swSsl, LV_STATE_CHECKED);

    updateInterfaceVisibility();
    updateIpModeVisibility();
    updateSslLabel();

    // Connection status pill: initial value + live updates.
    // Use getStateCopy() to avoid holding AppStore::mutex_ while already
    // under the display lock (prevents lock ordering inversion).
    updateStatusPill(AppStore::getInstance().getStateCopy());
    subscriptionId_ = AppStore::getInstance().subscribe([this](const AppState &state)
    {
        onStateChanged(state);
    });
}

NetworkSettingsPage::~NetworkSettingsPage()
{
    ESP_LOGI(TAG, "Destroying NetworkSettingsPage");
    AppStore::getInstance().unsubscribe(subscriptionId_);
}

void NetworkSettingsPage::render()
{
    // Static form: nothing to animate per frame
}

void NetworkSettingsPage::createUI()
{
    // Scrollable content column, children centered
    contentArea = lv_obj_create(get());
    lv_obj_set_size(contentArea, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(contentArea, 0, 0);
    lv_obj_set_style_bg_color(contentArea, theme_color_black, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(contentArea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(contentArea, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(contentArea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(contentArea, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(contentArea, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(contentArea, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(contentArea, 32, LV_PART_MAIN);
    lv_obj_set_style_pad_row(contentArea, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(contentArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(contentArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(contentArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(contentArea, LV_DIR_VER);

    // ── Header row: back button (left) + status pill (right) ──
    lv_obj_t *header = lv_obj_create(contentArea);
    lv_obj_set_size(header, formWidth_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t *btnBack = lv_button_create(header);
    lv_obj_set_size(btnBack, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_bg_opa(btnBack, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnBack, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnBack, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_radius(btnBack, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btnBack, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btnBack, 0, LV_PART_MAIN);
    lv_obj_t *backLabel = lv_label_create(btnBack);
    // Note: custom Roboto fonts have no LV_SYMBOL_* glyphs, use « instead
    lv_label_set_text(backLabel, "«  Retour");
    lv_obj_set_style_text_color(backLabel, theme_color_white, 0);
    lv_obj_set_style_text_font(backLabel, &roboto_medium_24, 0);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(btnBack, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        if (self->saving_)
            return;
        if (g_appMain && g_appMain->getStackView())
            g_appMain->getStackView()->pop(stack_animation_type::SlideVertical);
    }, LV_EVENT_CLICKED, this);

    // Status pill: colored dot + label
    statusPill = lv_obj_create(header);
    lv_obj_set_size(statusPill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(statusPill, theme_color_widget_bg_off, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(statusPill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(statusPill, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(statusPill, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_radius(statusPill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(statusPill, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(statusPill, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(statusPill, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(statusPill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusPill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(statusPill, LV_OBJ_FLAG_SCROLLABLE);

    statusDot = lv_obj_create(statusPill);
    lv_obj_set_size(statusDot, 12, 12);
    lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(statusDot, theme_color_red, LV_PART_MAIN);
    lv_obj_set_style_border_width(statusDot, 0, LV_PART_MAIN);

    statusLabel = lv_label_create(statusPill);
    lv_label_set_text(statusLabel, "");
    lv_obj_set_style_text_color(statusLabel, theme_color_white, 0);
    lv_obj_set_style_text_font(statusLabel, &roboto_regular_22, 0);

    // ── Page title ──
    lv_obj_t *title = lv_label_create(contentArea);
    lv_label_set_text(title, "Configuration réseau");
    lv_obj_set_width(title, formWidth_);
    lv_obj_set_style_text_color(title, theme_color_white, 0);
    lv_obj_set_style_text_font(title, &roboto_medium_28, 0);

    // ── Card: Interface & IP ──
    lv_obj_t *cardNet = createCard(contentArea, "Interface & IP");

    // Interface dropdown row
    lv_obj_t *rowIface = createFieldRow(cardNet, "Interface");
    ddInterface = lv_dropdown_create(rowIface);
    // Populate only with the interfaces this board actually has (touchlcd-7/8/10
    // are Wi-Fi only; the 86 panels have both). interfaceIds_ maps dropdown index
    // -> config value.
    {
        std::string opts;
        interfaceIds_.clear();
#if BOARD_HAS_ETHERNET
        interfaceIds_.push_back("ethernet");
        opts += "Ethernet";
#endif
#if BOARD_HAS_WIFI
        if (!opts.empty()) opts += "\n";
        interfaceIds_.push_back("wifi");
        opts += "Wi-Fi";
#endif
        if (interfaceIds_.empty())   // no flags set: fall back to Wi-Fi
        {
            interfaceIds_.push_back("wifi");
            opts = "Wi-Fi";
        }
        lv_dropdown_set_options(ddInterface, opts.c_str());
    }
    lv_obj_set_size(ddInterface, 240, 52);
    lv_obj_set_style_bg_color(ddInterface, color_input_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ddInterface, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ddInterface, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ddInterface, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_border_color(ddInterface, theme_color_blue, sel(LV_PART_MAIN, LV_STATE_FOCUSED));
    lv_obj_set_style_radius(ddInterface, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ddInterface, theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(ddInterface, &roboto_regular_24, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ddInterface, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ddInterface, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ddInterface, 0, LV_PART_MAIN);
    // The default dropdown arrow (LV_SYMBOL_DOWN) is a Montserrat glyph; the
    // Roboto UI font has no symbol range, so render the indicator with Montserrat
    // to avoid a missing-glyph box.
    lv_obj_set_style_text_font(ddInterface, &lv_font_montserrat_28_compressed, LV_PART_INDICATOR);
    // Single available interface: show it fixed (non-interactive) rather than a
    // one-item dropdown.
    if (interfaceIds_.size() <= 1)
        lv_obj_add_state(ddInterface, LV_STATE_DISABLED);
    lv_obj_t *ddList = lv_dropdown_get_list(ddInterface);
    lv_obj_set_style_bg_color(ddList, color_input_bg, LV_PART_MAIN);
    lv_obj_set_style_border_width(ddList, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ddList, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_radius(ddList, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ddList, theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(ddList, &roboto_regular_24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ddList, theme_color_blue, sel(LV_PART_SELECTED, LV_STATE_CHECKED));
    lv_obj_set_style_text_color(ddList, theme_color_black, sel(LV_PART_SELECTED, LV_STATE_CHECKED));
    lv_obj_add_event_cb(ddInterface, [](lv_event_t *e)
    {
        static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e))->updateInterfaceVisibility();
    }, LV_EVENT_VALUE_CHANGED, this);

    // Wi-Fi SSID + password (shown only when interface == Wi-Fi)
    rowSsid = createFieldRow(cardNet, "Réseau Wi-Fi (SSID)");
    taSsid = createTextArea(rowSsid, "Nom du réseau", 32);

    rowPassword = createFieldRow(cardNet, "Mot de passe");
    taPassword = createTextArea(rowPassword, "Mot de passe Wi-Fi", 64);
    lv_textarea_set_password_mode(taPassword, true);

    // Addressing mode: DHCP vs static IP
    lv_obj_t *rowMode = createFieldRow(cardNet, "Adressage");
    lv_obj_t *modeGroup = lv_obj_create(rowMode);
    lv_obj_set_size(modeGroup, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(modeGroup, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(modeGroup, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(modeGroup, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(modeGroup, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(modeGroup, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modeGroup, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(modeGroup, LV_OBJ_FLAG_SCROLLABLE);

    labelIpMode = lv_label_create(modeGroup);
    lv_label_set_text(labelIpMode, "DHCP");
    lv_obj_set_style_text_color(labelIpMode, theme_color_white, 0);
    lv_obj_set_style_text_font(labelIpMode, &roboto_regular_24, 0);

    swDhcp = createSwitch(modeGroup);
    lv_obj_add_event_cb(swDhcp, [](lv_event_t *e)
    {
        static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e))->updateIpModeVisibility();
    }, LV_EVENT_VALUE_CHANGED, this);

    // Static IP fields (shown only when "IP statique")
    rowStaticIp = createFieldRow(cardNet, "Adresse IP");
    taStaticIp = createTextArea(rowStaticIp, "192.168.1.10", 15);
    rowStaticMask = createFieldRow(cardNet, "Masque");
    taStaticMask = createTextArea(rowStaticMask, "255.255.255.0", 15);
    rowStaticGw = createFieldRow(cardNet, "Passerelle");
    taStaticGw = createTextArea(rowStaticGw, "192.168.1.1", 15);
    rowStaticDns = createFieldRow(cardNet, "DNS");
    taStaticDns = createTextArea(rowStaticDns, "9.9.9.9", 15);

    // ── Card: Serveur Calaos ──
    lv_obj_t *cardServer = createCard(contentArea, "Serveur Calaos");

    lv_obj_t *rowHost = createFieldRow(cardServer, "Serveur (hôte)");
    taServerHost = createTextArea(rowHost, "adresse IP ou nom d'hôte", 64);

    lv_obj_t *rowPort = createFieldRow(cardServer, "Port");
    taServerPort = createTextArea(rowPort, "5454", 5);
    lv_obj_set_flex_grow(taServerPort, 0);
    lv_obj_set_width(taServerPort, 160);

    lv_obj_t *rowSsl = createFieldRow(cardServer, "Chiffrement SSL");
    lv_obj_t *sslGroup = lv_obj_create(rowSsl);
    lv_obj_set_size(sslGroup, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sslGroup, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sslGroup, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sslGroup, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(sslGroup, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(sslGroup, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sslGroup, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sslGroup, LV_OBJ_FLAG_SCROLLABLE);

    labelSsl = lv_label_create(sslGroup);
    lv_label_set_text(labelSsl, "Non");
    lv_obj_set_style_text_color(labelSsl, theme_color_white, 0);
    lv_obj_set_style_text_font(labelSsl, &roboto_regular_24, 0);

    swSsl = createSwitch(sslGroup);
    lv_obj_add_event_cb(swSsl, [](lv_event_t *e)
    {
        static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e))->updateSslLabel();
    }, LV_EVENT_VALUE_CHANGED, this);

    // ── Inline error label ──
    errorLabel = lv_label_create(contentArea);
    lv_label_set_text(errorLabel, "");
    lv_obj_set_width(errorLabel, formWidth_);
    lv_label_set_long_mode(errorLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(errorLabel, theme_color_red, 0);
    lv_obj_set_style_text_font(errorLabel, &roboto_regular_24, 0);
    lv_obj_add_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);

    // ── Action buttons: Annuler / Enregistrer et redémarrer ──
    lv_obj_t *btnRow = lv_obj_create(contentArea);
    lv_obj_set_size(btnRow, formWidth_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btnRow, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_top(btnRow, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    btnCancel = lv_button_create(btnRow);
    lv_obj_set_flex_grow(btnCancel, 1);
    lv_obj_t *cancelLabel = nullptr;
    styleActionButton(btnCancel, &cancelLabel, "Annuler", false);
    lv_obj_add_event_cb(btnCancel, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        if (self->saving_)
            return;
        if (g_appMain && g_appMain->getStackView())
            g_appMain->getStackView()->pop(stack_animation_type::SlideVertical);
    }, LV_EVENT_CLICKED, this);

    btnSave = lv_button_create(btnRow);
    lv_obj_set_flex_grow(btnSave, 2);
    styleActionButton(btnSave, &btnSaveLabel, "Enregistrer et redémarrer", true);
    lv_obj_add_event_cb(btnSave, [](lv_event_t *e)
    {
        static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e))->onSaveClicked();
    }, LV_EVENT_CLICKED, this);

    // ── On-screen keyboard (overlay docked to the bottom, hidden by default) ──
    keyboard = lv_keyboard_create(get());
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(40));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(keyboard, LV_COLOR_MAKE(0x10, 0x10, 0x10), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard, color_input_bg, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, theme_color_white, LV_PART_ITEMS);
    // Montserrat is the only enabled font containing the LV_SYMBOL_* glyphs
    // used by the default keyboard maps (backspace, enter, ok, ...)
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_28_compressed, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
    // Do not steal focus from the textarea while typing
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        lv_obj_t *ta = lv_keyboard_get_textarea(self->keyboard);
        if (ta)
            lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        self->hideKeyboard();
    }, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        lv_obj_t *ta = lv_keyboard_get_textarea(self->keyboard);
        if (ta)
            lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        self->hideKeyboard();
    }, LV_EVENT_CANCEL, this);
}

lv_obj_t *NetworkSettingsPage::createCard(lv_obj_t *parent, const char *cardTitle)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, formWidth_);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme_color_widget_bg_off, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, cardTitle);
    lv_obj_set_style_text_color(titleLabel, theme_color_blue, 0);
    lv_obj_set_style_text_font(titleLabel, &roboto_medium_24, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, LV_PART_MAIN);

    return card;
}

lv_obj_t *NetworkSettingsPage::createFieldRow(lv_obj_t *card, const char *labelText)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 48, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *keyLabel = lv_label_create(row);
    lv_label_set_text(keyLabel, labelText);
    lv_obj_set_style_text_color(keyLabel, color_label_dim, 0);
    lv_obj_set_style_text_font(keyLabel, &roboto_regular_24, 0);

    return row;
}

lv_obj_t *NetworkSettingsPage::createTextArea(lv_obj_t *row, const char *placeholder, uint32_t maxLen)
{
    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    // One-line field: allow only horizontal scroll and hide the scrollbar, else
    // the cursor's scroll-to-view jitters the content vertically by a few px.
    lv_obj_set_scroll_dir(ta, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, maxLen);
    // Size to the natural one-line height (line + vertical padding). Forcing a
    // fixed height smaller than that makes the one-line textarea overflow
    // vertically and auto-scroll to the cursor, which jitters a few px forever.
    lv_obj_set_height(ta, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ta, 1);

    lv_obj_set_style_bg_color(ta, color_input_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 2, sel(LV_PART_MAIN, LV_STATE_FOCUSED));
    lv_obj_set_style_border_color(ta, theme_color_blue, sel(LV_PART_MAIN, LV_STATE_FOCUSED));
    lv_obj_set_style_radius(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ta, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &roboto_regular_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, color_placeholder, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, theme_color_blue, sel(LV_PART_CURSOR, LV_STATE_FOCUSED));
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, sel(LV_PART_CURSOR, LV_STATE_FOCUSED));

    lv_obj_add_event_cb(ta, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        self->showKeyboard(lv_event_get_target_obj(e));
    }, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(ta, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        self->showKeyboard(lv_event_get_target_obj(e));
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(ta, [](lv_event_t *e)
    {
        auto *self = static_cast<NetworkSettingsPage *>(lv_event_get_user_data(e));
        self->hideKeyboard();
    }, LV_EVENT_DEFOCUSED, this);

    return ta;
}

lv_obj_t *NetworkSettingsPage::createSwitch(lv_obj_t *parent)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 76, 44);
    lv_obj_set_style_bg_color(sw, LV_COLOR_MAKE(0x3A, 0x3A, 0x3A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, theme_color_blue, sel(LV_PART_INDICATOR, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, theme_color_white, LV_PART_KNOB);
    return sw;
}

void NetworkSettingsPage::styleActionButton(lv_obj_t *btn, lv_obj_t **labelOut, const char *text, bool primary)
{
    lv_obj_set_height(btn, 56);
    lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    if (primary)
    {
        lv_obj_set_style_bg_color(btn, theme_color_blue, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, theme_color_widget_border_off, LV_PART_MAIN);
    }
    lv_obj_set_style_opa(btn, LV_OPA_50, sel(LV_PART_MAIN, LV_STATE_DISABLED));

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, primary ? theme_color_black : theme_color_white, 0);
    lv_obj_set_style_text_font(label, &roboto_medium_24, 0);
    lv_obj_center(label);

    if (labelOut)
        *labelOut = label;
}

// ── Dynamic visibility ──────────────────────────────────────────────────────

void NetworkSettingsPage::updateInterfaceVisibility()
{
    uint32_t idx = lv_dropdown_get_selected(ddInterface);
    bool wifi = (idx < interfaceIds_.size() && interfaceIds_[idx] == "wifi");
    if (wifi)
    {
        lv_obj_clear_flag(rowSsid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rowPassword, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(rowSsid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rowPassword, LV_OBJ_FLAG_HIDDEN);
    }
}

void NetworkSettingsPage::updateIpModeVisibility()
{
    bool dhcp = lv_obj_has_state(swDhcp, LV_STATE_CHECKED);
    lv_label_set_text(labelIpMode, dhcp ? "DHCP" : "IP statique");

    lv_obj_t *rows[] = { rowStaticIp, rowStaticMask, rowStaticGw, rowStaticDns };
    for (lv_obj_t *row : rows)
    {
        if (dhcp)   // static fields visible only in static mode
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

void NetworkSettingsPage::updateSslLabel()
{
    bool ssl = lv_obj_has_state(swSsl, LV_STATE_CHECKED);
    lv_label_set_text(labelSsl, ssl ? "Oui" : "Non");
}

// ── Keyboard handling ───────────────────────────────────────────────────────

bool NetworkSettingsPage::isNumericField(lv_obj_t *ta) const
{
    return ta == taServerPort || ta == taStaticIp || ta == taStaticMask ||
           ta == taStaticGw || ta == taStaticDns;
}

void NetworkSettingsPage::showKeyboard(lv_obj_t *ta)
{
    if (!ta || saving_)
        return;

    lv_keyboard_set_textarea(keyboard, ta);
    lv_keyboard_set_mode(keyboard, isNumericField(ta) ? LV_KEYBOARD_MODE_NUMBER
                                                      : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    // Shrink the form area so the focused field can scroll above the keyboard.
    // Critical on the 720x720 square panels where the keyboard eats ~290px.
    lv_obj_set_height(contentArea, LV_PCT(60));
    lv_obj_update_layout(contentArea);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

void NetworkSettingsPage::hideKeyboard()
{
    if (lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN))
        return;

    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_set_height(contentArea, LV_PCT(100));
}

// ── Validation & save ───────────────────────────────────────────────────────

bool NetworkSettingsPage::isValidIpv4(const std::string &s)
{
    if (s.empty() || s.size() > 15)
        return false;

    int octets = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;

        size_t start = i;
        int value = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        {
            value = value * 10 + (s[i] - '0');
            i++;
        }

        size_t digits = i - start;
        if (digits == 0 || digits > 3 || value > 255)
            return false;
        // Reject leading zeros like "01"
        if (digits > 1 && s[start] == '0')
            return false;

        octets++;
        if (octets > 4)
            return false;

        if (i < s.size())
        {
            if (s[i] != '.')
                return false;
            i++;
            if (i == s.size()) // trailing dot
                return false;
        }
    }

    return octets == 4;
}

bool NetworkSettingsPage::validate(std::string &errorMsg)
{
    uint32_t ifIdx = lv_dropdown_get_selected(ddInterface);
    bool wifi = (ifIdx < interfaceIds_.size() && interfaceIds_[ifIdx] == "wifi");
    bool staticIp = !lv_obj_has_state(swDhcp, LV_STATE_CHECKED);

    if (wifi && trimmed(lv_textarea_get_text(taSsid)).empty())
    {
        errorMsg = "Le nom du réseau Wi-Fi (SSID) est requis.";
        return false;
    }

    if (staticIp)
    {
        if (!isValidIpv4(trimmed(lv_textarea_get_text(taStaticIp))))
        {
            errorMsg = "Adresse IP invalide. Format attendu : 192.168.1.10";
            return false;
        }
        if (!isValidIpv4(trimmed(lv_textarea_get_text(taStaticMask))))
        {
            errorMsg = "Masque invalide. Format attendu : 255.255.255.0";
            return false;
        }
        if (!isValidIpv4(trimmed(lv_textarea_get_text(taStaticGw))))
        {
            errorMsg = "Passerelle invalide. Format attendu : 192.168.1.1";
            return false;
        }
        std::string dns = trimmed(lv_textarea_get_text(taStaticDns));
        if (!dns.empty() && !isValidIpv4(dns))
        {
            errorMsg = "DNS invalide. Format attendu : 9.9.9.9 (ou laisser vide)";
            return false;
        }
    }

    if (trimmed(lv_textarea_get_text(taServerHost)).empty())
    {
        errorMsg = "Le serveur Calaos (hôte) est requis.";
        return false;
    }

    std::string portStr = trimmed(lv_textarea_get_text(taServerPort));
    bool portOk = !portStr.empty() && portStr.size() <= 5 &&
                  std::all_of(portStr.begin(), portStr.end(),
                              [](unsigned char c) { return std::isdigit(c); });
    if (portOk)
    {
        long port = std::stol(portStr);
        portOk = (port >= 1 && port <= 65535);
    }
    if (!portOk)
    {
        errorMsg = "Port invalide. Entrez un nombre entre 1 et 65535.";
        return false;
    }

    return true;
}

void NetworkSettingsPage::showError(const std::string &msg)
{
    lv_label_set_text(errorLabel, msg.c_str());
    lv_obj_set_style_text_color(errorLabel, theme_color_red, 0);
    lv_obj_clear_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(contentArea);
    lv_obj_scroll_to_view(errorLabel, LV_ANIM_ON);
}

void NetworkSettingsPage::onSaveClicked()
{
    if (saving_)
        return;

    hideKeyboard();

    std::string errorMsg;
    if (!validate(errorMsg))
    {
        showError(errorMsg);
        return;
    }

    // Build the new config from the form
    CalaosConfig cfg;
    {
        uint32_t idx = lv_dropdown_get_selected(ddInterface);
        cfg.networkInterface = (idx < interfaceIds_.size()) ? interfaceIds_[idx] : "wifi";
    }
    cfg.ipMode = lv_obj_has_state(swDhcp, LV_STATE_CHECKED) ? "dhcp" : "static";
    cfg.staticIp = trimmed(lv_textarea_get_text(taStaticIp));
    cfg.staticMask = trimmed(lv_textarea_get_text(taStaticMask));
    cfg.staticGateway = trimmed(lv_textarea_get_text(taStaticGw));
    cfg.staticDns = trimmed(lv_textarea_get_text(taStaticDns));
    cfg.wifiSsid = trimmed(lv_textarea_get_text(taSsid));
    cfg.wifiPassword = lv_textarea_get_text(taPassword); // passwords may contain spaces
    cfg.serverHost = trimmed(lv_textarea_get_text(taServerHost));
    cfg.serverPort = static_cast<uint16_t>(std::stol(trimmed(lv_textarea_get_text(taServerPort))));
    cfg.serverSsl = lv_obj_has_state(swSsl, LV_STATE_CHECKED);
    cfg.hasServerHost = !cfg.serverHost.empty();

    ESP_LOGI(TAG, "Saving config: interface=%s ip_mode=%s server=%s:%u ssl=%d",
             cfg.networkInterface.c_str(), cfg.ipMode.c_str(),
             cfg.serverHost.c_str(), cfg.serverPort, cfg.serverSsl);

    if (HAL::getInstance().getSystem().saveDeviceConfig(cfg) != HalResult::OK)
    {
        showError("Échec de l'enregistrement de la configuration. Veuillez réessayer.");
        return;
    }

    // Keep RAM copy in sync so nothing reads stale values before reboot
    DeviceConfig::getInstance().setConfig(cfg);

    saving_ = true;
    lv_obj_add_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(btnSaveLabel, "Redémarrage...");
    lv_obj_add_state(btnSave, LV_STATE_DISABLED);
    lv_obj_add_state(btnCancel, LV_STATE_DISABLED);

    ESP_LOGI(TAG, "Config saved, restarting device");
    LvglTimer::createOneShot([]()
    {
        HAL::getInstance().getSystem().restart();
    }, 900);
}

// ── Connection status pill ──────────────────────────────────────────────────

void NetworkSettingsPage::updateStatusPill(const AppState &state)
{
    if (state.websocket.isConnected)
    {
        lv_label_set_text(statusLabel, "Connecté");
        lv_obj_set_style_bg_color(statusDot, theme_color_green, LV_PART_MAIN);
    }
    else if (state.network.hasTimeout || state.calaosServer.hasTimeout)
    {
        lv_label_set_text(statusLabel, "Échec de connexion");
        lv_obj_set_style_bg_color(statusDot, theme_color_red, LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text(statusLabel, "Connexion...");
        lv_obj_set_style_bg_color(statusDot, theme_color_yellow, LV_PART_MAIN);
    }
}

void NetworkSettingsPage::onStateChanged(const AppState &state)
{
    // Called from the dispatcher thread: take the display lock non-blocking
    if (HAL::getInstance().getDisplay().tryLock(100))
    {
        updateStatusPill(state);
        HAL::getInstance().getDisplay().unlock();
    }
}

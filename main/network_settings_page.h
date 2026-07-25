#pragma once

#include "page_base.h"
#include "flux.h"
#include "lvgl_timer.h"
#include <memory>
#include <string>
#include <vector>

// On-device network/connection settings page.
// Lets the user reconfigure interface (Wi-Fi/Ethernet), IP mode (DHCP/static),
// Wi-Fi credentials and Calaos server (host/port/SSL) without reflashing.
// Saving persists to the "config" flash partition and reboots the device.
class NetworkSettingsPage : public PageBase
{
public:
    NetworkSettingsPage(lv_obj_t *parent);
    ~NetworkSettingsPage();

    void render() override;

private:
    // UI construction
    void createUI();
    lv_obj_t *createCard(lv_obj_t *parent, const char *title);
    lv_obj_t *createFieldRow(lv_obj_t *card, const char *labelText);
    lv_obj_t *createTextArea(lv_obj_t *row, const char *placeholder, uint32_t maxLen);
    lv_obj_t *createSwitch(lv_obj_t *row);
    void styleActionButton(lv_obj_t *btn, lv_obj_t **labelOut, const char *text, bool primary);

    // Dynamic field visibility
    void updateInterfaceVisibility();
    void updateIpModeVisibility();
    void updateSslLabel();

    // Keyboard handling
    void showKeyboard(lv_obj_t *ta);
    void hideKeyboard();
    bool isNumericField(lv_obj_t *ta) const;

    // Save / validation
    void onSaveClicked();
    bool validate(std::string &errorMsg);
    void showError(const std::string &msg);
    static bool isValidIpv4(const std::string &s);

    // Connection status pill
    void updateStatusPill(const AppState &state);
    void onStateChanged(const AppState &state);

    // Containers
    lv_obj_t *contentArea = nullptr;
    lv_obj_t *keyboard = nullptr;

    // Status pill
    lv_obj_t *statusPill = nullptr;
    lv_obj_t *statusDot = nullptr;
    lv_obj_t *statusLabel = nullptr;

    // Interface & IP card controls
    lv_obj_t *ddInterface = nullptr;
    lv_obj_t *rowSsid = nullptr;
    lv_obj_t *taSsid = nullptr;
    lv_obj_t *rowPassword = nullptr;
    lv_obj_t *taPassword = nullptr;
    lv_obj_t *swDhcp = nullptr;   // checked = DHCP, unchecked = static IP
    lv_obj_t *labelIpMode = nullptr;
    lv_obj_t *rowStaticIp = nullptr;
    lv_obj_t *taStaticIp = nullptr;
    lv_obj_t *rowStaticMask = nullptr;
    lv_obj_t *taStaticMask = nullptr;
    lv_obj_t *rowStaticGw = nullptr;
    lv_obj_t *taStaticGw = nullptr;
    lv_obj_t *rowStaticDns = nullptr;
    lv_obj_t *taStaticDns = nullptr;

    // Server card controls
    lv_obj_t *taServerHost = nullptr;
    lv_obj_t *taServerPort = nullptr;
    lv_obj_t *swSsl = nullptr;
    lv_obj_t *labelSsl = nullptr;

    // Error + actions
    lv_obj_t *errorLabel = nullptr;
    lv_obj_t *btnCancel = nullptr;
    lv_obj_t *btnSave = nullptr;
    lv_obj_t *btnSaveLabel = nullptr;

    bool saving_ = false;
    int32_t formWidth_ = 640;
    std::vector<std::string> interfaceIds_;   // dropdown index -> "ethernet"/"wifi" (board-gated)

    SubscriptionId subscriptionId_;
};

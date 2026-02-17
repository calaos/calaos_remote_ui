#include "about_page.h"
#include "theme.h"
#include "hal.h"
#include "logging.h"
#include "version.h"
#include "images_generated.h"
#include <cstdio>

static const char *TAG = "AboutPage";

AboutPage::AboutPage(lv_obj_t *parent):
    scrollContainer(nullptr),
    statusDot(nullptr),
    labelServerIpVal(nullptr),
    labelWsStatusVal(nullptr),
    labelLastConnectionVal(nullptr),
    labelConnectionTypeVal(nullptr),
    labelLocalIpVal(nullptr),
    labelGatewayVal(nullptr),
    labelNetmaskVal(nullptr),
    labelMacVal(nullptr),
    labelSsidRow(nullptr),
    labelSsidVal(nullptr),
    labelRssiRow(nullptr),
    labelRssiVal(nullptr),
    labelUptimeVal(nullptr),
    labelNtpVal(nullptr),
    labelDeviceIdVal(nullptr),
    labelDeviceInfoVal(nullptr)
{
    ESP_LOGI(TAG, "Creating AboutPage");

    createUI(parent);

    // Subscribe to state changes
    subscriptionId_ = AppStore::getInstance().subscribe([this](const AppState &state)
    {
        onStateChanged(state);
    });

    // Initial update
    const AppState &initialState = AppStore::getInstance().getState();
    updateDynamicLabels(initialState);

    // Timer for uptime refresh every second
    uptimeTimer = LvglTimer::createRepeating([this]()
    {
        updateUptime();
    }, 1000);

    ESP_LOGI(TAG, "AboutPage created");
}

AboutPage::~AboutPage()
{
    ESP_LOGI(TAG, "Destroying AboutPage");
    AppStore::getInstance().unsubscribe(subscriptionId_);
    uptimeTimer.reset();
}

void AboutPage::createUI(lv_obj_t *parent)
{
    // Create scrollable container filling the tab
    scrollContainer = lv_obj_create(parent);
    lv_obj_set_size(scrollContainer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrollContainer, theme_color_black, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scrollContainer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(scrollContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(scrollContainer, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_right(scrollContainer, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_top(scrollContainer, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(scrollContainer, 40, LV_PART_MAIN);
    lv_obj_set_flex_flow(scrollContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scrollContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scrollContainer, 12, LV_PART_MAIN);
    lv_obj_add_flag(scrollContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scrollContainer, LV_DIR_VER);

    // ── Header: Logo + App name + Version ──
    lv_obj_t *logoImg = lv_image_create(scrollContainer);
    lv_image_set_src(logoImg, &logo_full);
    lv_obj_set_style_pad_top(logoImg, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(logoImg, 4, LV_PART_MAIN);

    char versionBuf[64];
    snprintf(versionBuf, sizeof(versionBuf), "Version %s", APP_VERSION);
    lv_obj_t *versionLabel = lv_label_create(scrollContainer);
    lv_label_set_text(versionLabel, versionBuf);
    lv_obj_set_style_text_color(versionLabel, theme_color_blue, 0);
    lv_obj_set_style_text_font(versionLabel, &roboto_medium_22, 0);
    lv_obj_set_style_pad_bottom(versionLabel, 8, LV_PART_MAIN);

    // ── Card: Server Connection ──
    lv_obj_t *serverCard = createCard(scrollContainer, "Server");
    createInfoRow(serverCard, "Address", &labelServerIpVal);

    // Status row: key left, dot+value right (same SPACE_BETWEEN as other rows)
    lv_obj_t *statusRow = lv_obj_create(serverCard);
    lv_obj_set_size(statusRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statusRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(statusRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(statusRow, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *statusKeyLabel = lv_label_create(statusRow);
    lv_label_set_text(statusKeyLabel, "Status");
    lv_obj_set_style_text_color(statusKeyLabel, lv_color_make(0x88, 0x88, 0x88), 0);
    lv_obj_set_style_text_font(statusKeyLabel, &roboto_regular_22, 0);

    // Right side: dot + value grouped together
    lv_obj_t *statusValContainer = lv_obj_create(statusRow);
    lv_obj_set_size(statusValContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statusValContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(statusValContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(statusValContainer, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(statusValContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusValContainer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(statusValContainer, 8, LV_PART_MAIN);

    statusDot = lv_obj_create(statusValContainer);
    lv_obj_set_size(statusDot, 10, 10);
    lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(statusDot, theme_color_green, LV_PART_MAIN);
    lv_obj_set_style_border_width(statusDot, 0, LV_PART_MAIN);

    labelWsStatusVal = lv_label_create(statusValContainer);
    lv_label_set_text(labelWsStatusVal, "");
    lv_obj_set_style_text_color(labelWsStatusVal, theme_color_white, 0);
    lv_obj_set_style_text_font(labelWsStatusVal, &roboto_regular_22, 0);

    createInfoRow(serverCard, "Last connection", &labelLastConnectionVal);

    // ── Card: Network ──
    lv_obj_t *networkCard = createCard(scrollContainer, "Network");
    createInfoRow(networkCard, "Type", &labelConnectionTypeVal);
    createInfoRow(networkCard, "Local IP", &labelLocalIpVal);
    createInfoRow(networkCard, "Gateway", &labelGatewayVal);
    createInfoRow(networkCard, "Netmask", &labelNetmaskVal);
    createInfoRow(networkCard, "MAC", &labelMacVal);
    labelSsidRow = createInfoRow(networkCard, "SSID", &labelSsidVal);
    labelRssiRow = createInfoRow(networkCard, "RSSI", &labelRssiVal);

    // ── Card: System ──
    lv_obj_t *systemCard = createCard(scrollContainer, "System");
    createInfoRow(systemCard, "Uptime", &labelUptimeVal);
    createInfoRow(systemCard, "NTP", &labelNtpVal);
    createInfoRow(systemCard, "Device", &labelDeviceInfoVal);
    createInfoRow(systemCard, "Device ID", &labelDeviceIdVal);
}

lv_obj_t *AboutPage::createCard(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme_color_widget_bg_off, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, theme_color_widget_border_off, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Card title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_color(titleLabel, theme_color_blue, 0);
    lv_obj_set_style_text_font(titleLabel, &roboto_medium_24, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, LV_PART_MAIN);

    return card;
}

lv_obj_t *AboutPage::createInfoRow(lv_obj_t *card, const char *key, lv_obj_t **valueLabel)
{
    // Row container: key on left, value on right
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Key label (dimmed)
    lv_obj_t *keyLabel = lv_label_create(row);
    lv_label_set_text(keyLabel, key);
    lv_obj_set_style_text_color(keyLabel, lv_color_make(0x88, 0x88, 0x88), 0);
    lv_obj_set_style_text_font(keyLabel, &roboto_regular_22, 0);

    // Value label (bright white)
    *valueLabel = lv_label_create(row);
    lv_label_set_text(*valueLabel, "--");
    lv_obj_set_style_text_color(*valueLabel, theme_color_white, 0);
    lv_obj_set_style_text_font(*valueLabel, &roboto_regular_22, 0);
    lv_label_set_long_mode(*valueLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(*valueLabel, 1);
    lv_obj_set_style_text_align(*valueLabel, LV_TEXT_ALIGN_RIGHT, 0);

    return row;
}

void AboutPage::updateDynamicLabels(const AppState &state)
{
    // Server IP
    const std::string &server = state.calaosServer.selectedServer;
    lv_label_set_text(labelServerIpVal, server.empty() ? "--" : server.c_str());

    // WebSocket status + dot color
    if (state.websocket.isConnected)
    {
        lv_label_set_text(labelWsStatusVal, "Connected");
        lv_obj_set_style_bg_color(statusDot, theme_color_green, LV_PART_MAIN);
    }
    else if (state.websocket.isConnecting)
    {
        lv_label_set_text(labelWsStatusVal, "Connecting...");
        lv_obj_set_style_bg_color(statusDot, theme_color_yellow, LV_PART_MAIN);
    }
    else if (state.websocket.hasError)
    {
        lv_label_set_text(labelWsStatusVal, "Error");
        lv_obj_set_style_bg_color(statusDot, theme_color_red, LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text(labelWsStatusVal, "Disconnected");
        lv_obj_set_style_bg_color(statusDot, lv_color_make(0x66, 0x66, 0x66), LV_PART_MAIN);
    }

    // Last connection
    if (state.websocket.lastConnectedTimestamp > 0)
    {
        uint64_t now = HAL::getInstance().getSystem().getTimeMs();
        uint64_t elapsed = now - state.websocket.lastConnectedTimestamp;
        char durationBuf[64];
        formatDuration(elapsed, durationBuf, sizeof(durationBuf));
        char buf[128];
        snprintf(buf, sizeof(buf), "%s ago", durationBuf);
        lv_label_set_text(labelLastConnectionVal, buf);
    }
    else
    {
        lv_label_set_text(labelLastConnectionVal, "--");
    }

    // Connection type
    const char *connType = "None";
    if (state.network.connectionType == NetworkConnectionType::WiFi)
        connType = "WiFi";
    else if (state.network.connectionType == NetworkConnectionType::Ethernet)
        connType = "Ethernet";
    lv_label_set_text(labelConnectionTypeVal, connType);

    // Local IP
    lv_label_set_text(labelLocalIpVal,
                      state.network.ipAddress.empty() ? "--" : state.network.ipAddress.c_str());

    // Gateway
    lv_label_set_text(labelGatewayVal,
                      state.network.gateway.empty() ? "--" : state.network.gateway.c_str());

    // Netmask
    lv_label_set_text(labelNetmaskVal,
                      state.network.netmask.empty() ? "--" : state.network.netmask.c_str());

    // MAC
    std::string mac = HAL::getInstance().getNetwork().getMacAddress();
    lv_label_set_text(labelMacVal, mac.empty() ? "--" : mac.c_str());

    // WiFi specific: SSID & RSSI
    if (state.network.connectionType == NetworkConnectionType::WiFi)
    {
        lv_label_set_text(labelSsidVal,
                          state.network.ssid.empty() ? "--" : state.network.ssid.c_str());
        lv_obj_clear_flag(labelSsidRow, LV_OBJ_FLAG_HIDDEN);

        char rssiBuf[32];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", state.network.rssi);
        lv_label_set_text(labelRssiVal, rssiBuf);
        lv_obj_clear_flag(labelRssiRow, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(labelSsidRow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(labelRssiRow, LV_OBJ_FLAG_HIDDEN);
    }

    // NTP
    const char *ntpStatus = state.ntp.isSynced ? "Synchronized" :
                            state.ntp.isSyncing ? "Synchronizing..." : "Not synchronized";
    lv_label_set_text(labelNtpVal, ntpStatus);

    // Device info
    std::string deviceInfo = HAL::getInstance().getSystem().getDeviceInfo();
    lv_label_set_text(labelDeviceInfoVal, deviceInfo.c_str());

    // Device ID
    lv_label_set_text(labelDeviceIdVal,
                      state.provisioning.deviceId.empty() ? "--" : state.provisioning.deviceId.c_str());
}

void AboutPage::updateUptime()
{
    uint64_t uptimeMs = HAL::getInstance().getSystem().getTimeMs();
    char durationBuf[64];
    formatDuration(uptimeMs, durationBuf, sizeof(durationBuf));
    lv_label_set_text(labelUptimeVal, durationBuf);

    // Also refresh last connection time
    const AppState &state = AppStore::getInstance().getState();
    if (state.websocket.lastConnectedTimestamp > 0)
    {
        uint64_t elapsed = uptimeMs - state.websocket.lastConnectedTimestamp;
        formatDuration(elapsed, durationBuf, sizeof(durationBuf));
        char buf[128];
        snprintf(buf, sizeof(buf), "%s ago", durationBuf);
        lv_label_set_text(labelLastConnectionVal, buf);
    }
    else
    {
        lv_label_set_text(labelLastConnectionVal, "--");
    }
}

void AboutPage::onStateChanged(const AppState &state)
{
    if (HAL::getInstance().getDisplay().tryLock(100))
    {
        updateDynamicLabels(state);
        HAL::getInstance().getDisplay().unlock();
    }
}

void AboutPage::formatDuration(uint64_t ms, char *buf, size_t bufSize)
{
    uint64_t totalSeconds = ms / 1000;
    uint64_t years = totalSeconds / (365 * 86400);
    uint64_t remaining = totalSeconds % (365 * 86400);
    uint64_t months = remaining / (30 * 86400);
    remaining = remaining % (30 * 86400);
    uint64_t weeks = remaining / (7 * 86400);
    remaining = remaining % (7 * 86400);
    uint64_t days = remaining / 86400;
    uint64_t hours = (remaining % 86400) / 3600;
    uint64_t minutes = (remaining % 3600) / 60;
    uint64_t seconds = remaining % 60;

    char tmp[128] = "";
    int pos = 0;

    if (years > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%lluy ", (unsigned long long)years);
    if (months > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%llumo ", (unsigned long long)months);
    if (weeks > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%lluw ", (unsigned long long)weeks);
    if (days > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%llud ", (unsigned long long)days);
    if (hours > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%lluh ", (unsigned long long)hours);
    if (minutes > 0)
        pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%llum ", (unsigned long long)minutes);

    snprintf(tmp + pos, sizeof(tmp) - pos, "%llus", (unsigned long long)seconds);

    snprintf(buf, bufSize, "%s", tmp);
}

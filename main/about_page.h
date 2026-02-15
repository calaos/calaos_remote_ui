#pragma once

#include "lvgl.h"
#include "flux.h"
#include "lvgl_timer.h"
#include <memory>

class AboutPage
{
public:
    AboutPage(lv_obj_t *parent);
    ~AboutPage();

    AboutPage(const AboutPage&) = delete;
    AboutPage& operator=(const AboutPage&) = delete;

private:
    lv_obj_t *scrollContainer;

    // Status indicator dot
    lv_obj_t *statusDot;

    // Card: Server connection (value labels)
    lv_obj_t *labelServerIpVal;
    lv_obj_t *labelWsStatusVal;
    lv_obj_t *labelLastConnectionVal;

    // Card: Network (value labels)
    lv_obj_t *labelConnectionTypeVal;
    lv_obj_t *labelLocalIpVal;
    lv_obj_t *labelGatewayVal;
    lv_obj_t *labelNetmaskVal;
    lv_obj_t *labelMacVal;
    lv_obj_t *labelSsidRow;
    lv_obj_t *labelSsidVal;
    lv_obj_t *labelRssiRow;
    lv_obj_t *labelRssiVal;

    // Card: System (value labels)
    lv_obj_t *labelUptimeVal;
    lv_obj_t *labelNtpVal;
    lv_obj_t *labelDeviceIdVal;
    lv_obj_t *labelDeviceInfoVal;

    // Timer for uptime refresh
    std::unique_ptr<LvglTimer> uptimeTimer;

    // AppStore subscription
    SubscriptionId subscriptionId_;

    void createUI(lv_obj_t *parent);
    lv_obj_t *createCard(lv_obj_t *parent, const char *title);
    lv_obj_t *createInfoRow(lv_obj_t *card, const char *key, lv_obj_t **valueLabel);
    void updateDynamicLabels(const AppState &state);
    void updateUptime();
    void onStateChanged(const AppState &state);

    static void formatDuration(uint64_t ms, char *buf, size_t bufSize);
};

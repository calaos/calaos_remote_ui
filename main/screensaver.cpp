#include "screensaver.h"
#include "theme.h"
#include "logging.h"
#include "hal.h"
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>

static const char* TAG = "screensaver";

struct FontEntry
{
    const lv_font_t* font;
    int lineHeight;
};

static const FontEntry timeFonts[] = {
    { &roboto_bold_150, 150 },
    { &roboto_bold_120, 120 },
    { &roboto_bold_96, 96 },
    { &roboto_bold_72, 72 },
    { &roboto_bold_60, 60 },
    { &roboto_bold_50, 50 },
    { &roboto_bold_48, 48 },
    { &roboto_bold_32, 32 },
    { &roboto_bold_28, 28 },
};

static const FontEntry dateFonts[] = {
    { &roboto_regular_60, 60 },
    { &roboto_regular_48, 48 },
    { &roboto_regular_32, 32 },
    { &roboto_regular_28, 28 },
    { &roboto_regular_26, 26 },
    { &roboto_regular_24, 24 },
};

ScreenSaver::ScreenSaver(lv_obj_t* parent):
    Container(parent)
{
    createUI();

    subscriptionId_ = AppStore::getInstance().subscribe(
        [this](const AppState& state)
        {
            applyConfig(state.config);
        });

    // Apply initial config if available
    auto stateCopy = AppStore::getInstance().getStateCopy();
    applyConfig(stateCopy.config);

    ESP_LOGI(TAG, "ScreenSaver created (timeout=%ds, dimming=%d, mode=%s)",
             screensaverTimeout_, screensaverDimming_, screensaverMode_.c_str());
}

ScreenSaver::~ScreenSaver()
{
    if (subscriptionId_)
        AppStore::getInstance().unsubscribe(subscriptionId_);
}

void ScreenSaver::createUI()
{
    // Full screen black overlay
    lv_obj_set_size(get(), LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(get(), 0, 0);
    setBgColor(lv_color_black());
    lv_obj_set_style_bg_opa(get(), LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(get(), 0, 0);
    lv_obj_set_style_radius(get(), 0, 0);
    lv_obj_set_style_pad_all(get(), 0, 0);

    // Clickable to intercept touch and wake up
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(get(), onClicked, LV_EVENT_CLICKED, this);

    // Column flex layout, centered (for clock mode)
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Hidden by default
    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);
}

void ScreenSaver::rebuildClockUI()
{
    // Remove old labels
    if (timeLabel)
    {
        lv_obj_delete(timeLabel);
        timeLabel = nullptr;
    }
    if (dateLabel)
    {
        lv_obj_delete(dateLabel);
        dateLabel = nullptr;
    }

    if (screensaverMode_ != "clock")
        return;

    DisplayInfo displayInfo = HAL::getInstance().getDisplay().getDisplayInfo();
    int screenW = displayInfo.width;
    int screenH = displayInfo.height;
    int padding = 40;
    int innerW = screenW - 2 * padding;
    int innerH = screenH - 2 * padding;

    lv_obj_set_style_pad_all(get(), padding, 0);

    int timeFontHeight;
    if (clockShowDate_)
    {
        int gap = 8;
        timeFontHeight = (innerH * 65) / 100 - gap;
    }
    else
    {
        timeFontHeight = innerH;
    }

    const char* timeRef;
    if (clockUse24h_)
        timeRef = clockShowSeconds_ ? "23:59:59" : "23:59";
    else
        timeRef = clockShowSeconds_ ? "12:59:59 PM" : "12:59 PM";

    const lv_font_t* timeFont = selectFont(timeFontHeight, innerW, timeRef);
    ESP_LOGD(TAG, "rebuildClockUI: screen=%dx%d inner=%dx%d timeFontHeight=%d → timeFont line_height=%d",
             screenW, screenH, innerW, innerH, timeFontHeight,
             timeFont ? (int)timeFont->line_height : -1);

    timeLabel = lv_label_create(get());
    lv_label_set_text(timeLabel, "--:--");
    lv_obj_set_style_text_font(timeLabel, timeFont, 0);
    lv_obj_set_style_text_color(timeLabel, theme_color_white, 0);
    lv_obj_set_style_text_align(timeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(timeLabel, innerW);
    lv_label_set_long_mode(timeLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);

    if (clockShowDate_)
    {
        int dateFontHeight = (innerH * 35) / 100 - 8;
        const char* dateRef = "24/02/2026";
        const lv_font_t* dateFont = selectFont(dateFontHeight, innerW, dateRef);
        if (!dateFont)
            dateFont = &roboto_regular_24;
        ESP_LOGD(TAG, "rebuildClockUI: dateFontHeight=%d → dateFont line_height=%d",
                 dateFontHeight, dateFont ? (int)dateFont->line_height : -1);

        dateLabel = lv_label_create(get());
        lv_label_set_text(dateLabel, "");
        lv_obj_set_style_text_font(dateLabel, dateFont, 0);
        lv_obj_set_style_text_color(dateLabel, theme_color_blue, 0);
        lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(dateLabel, innerW);
        lv_label_set_long_mode(dateLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
}

const lv_font_t* ScreenSaver::selectFont(int availableHeight, int availableWidth,
                                          const char* referenceText)
{
    int fontCount = sizeof(timeFonts) / sizeof(timeFonts[0]);
    for (int i = 0; i < fontCount; i++)
    {
        if (timeFonts[i].lineHeight > availableHeight)
            continue;

        lv_point_t textSize;
        lv_txt_get_size(&textSize, referenceText, timeFonts[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        if (textSize.x <= availableWidth)
            return timeFonts[i].font;
    }
    return timeFonts[fontCount - 1].font;
}

void ScreenSaver::onClicked(lv_event_t* e)
{
    auto* self = static_cast<ScreenSaver*>(lv_event_get_user_data(e));
    if (self && self->active_)
    {
        ESP_LOGI(TAG, "Touch detected on screensaver overlay, waking up");
        self->deactivate();
    }
}

void ScreenSaver::update()
{
    if (screensaverTimeout_ <= 0)
    {
        if (active_)
        {
            ESP_LOGD(TAG, "Timeout is 0, deactivating screensaver");
            deactivate();
        }
        return;
    }

    if (active_)
        return;

    uint32_t inactiveMs = lv_display_get_inactive_time(nullptr);
    uint32_t timeoutMs = static_cast<uint32_t>(screensaverTimeout_) * 1000;

    // Log periodically (every ~5s) to avoid flooding
    static uint32_t lastLogMs = 0;
    if (inactiveMs - lastLogMs >= 5000)
    {
        ESP_LOGD(TAG, "Inactivity: %lu ms / %lu ms timeout",
                 (unsigned long)inactiveMs, (unsigned long)timeoutMs);
        lastLogMs = inactiveMs;
    }

    if (inactiveMs >= timeoutMs)
    {
        ESP_LOGI(TAG, "Inactivity timeout reached (%lu ms >= %lu ms), activating",
                 (unsigned long)inactiveMs, (unsigned long)timeoutMs);
        activate();
    }
}

void ScreenSaver::activate()
{
    if (active_)
        return;

    ESP_LOGI(TAG, "Activating screensaver (mode=%s, dimming=%d)", screensaverMode_.c_str(), screensaverDimming_);

    active_ = true;

    lv_obj_remove_flag(get(), LV_OBJ_FLAG_HIDDEN);

    // Set backlight to screensaver dimming level
    HAL::getInstance().getDisplay().setBacklight(static_cast<uint8_t>(screensaverDimming_));

    if (screensaverMode_ == "clock")
    {
        if (timeLabel)
            lv_obj_remove_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
        if (dateLabel)
            lv_obj_remove_flag(dateLabel, LV_OBJ_FLAG_HIDDEN);

        updateClock();

        clockTimer = LvglTimer::createRepeating(
            [this]()
            {
                updateClock();
            }, 1000);
    }
    else
    {
        // Black mode - hide clock labels if they exist
        if (timeLabel)
            lv_obj_add_flag(timeLabel, LV_OBJ_FLAG_HIDDEN);
        if (dateLabel)
            lv_obj_add_flag(dateLabel, LV_OBJ_FLAG_HIDDEN);
    }

    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ScreenSaverActivated));
}

void ScreenSaver::deactivate()
{
    if (!active_)
        return;

    ESP_LOGI(TAG, "Deactivating screensaver, restoring brightness to %d", normalBrightness_);

    active_ = false;

    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);

    // Restore normal brightness
    HAL::getInstance().getDisplay().setBacklight(static_cast<uint8_t>(normalBrightness_));

    // Stop clock timer
    clockTimer.reset();

    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::ScreenSaverDeactivated));
}

void ScreenSaver::applyConfig(const CalaosProtocol::RemoteUIConfig& config)
{
    ESP_LOGD(TAG, "Config update: timeout=%d, dimming=%d, mode=%s, brightness=%d",
             config.screensaver_timeout, config.screensaver_dimming,
             config.screensaver_mode.c_str(), config.brightness);
    ESP_LOGD(TAG, "Clock config: tz=%s, format=%s, show_date=%s, date_fmt=%s",
             config.screensaver_clock_timezone.c_str(), config.screensaver_clock_format.c_str(),
             config.screensaver_clock_show_date.c_str(), config.screensaver_clock_date_format.c_str());

    bool modeChanged = (screensaverMode_ != config.screensaver_mode);
    bool clockConfigChanged = modeChanged ||
        (clockTimezone_ != config.screensaver_clock_timezone) ||
        (clockUse24h_ != (config.screensaver_clock_format != "12")) ||
        (clockShowDate_ != (config.screensaver_clock_show_date == "true")) ||
        (clockShowSeconds_ != (config.screensaver_clock_seconds == "true")) ||
        (clockDateFormat_ != config.screensaver_clock_date_format);

    screensaverTimeout_ = config.screensaver_timeout;
    screensaverDimming_ = config.screensaver_dimming;
    normalBrightness_ = config.brightness;
    screensaverMode_ = config.screensaver_mode;
    clockTimezone_ = config.screensaver_clock_timezone;
    clockUse24h_ = (config.screensaver_clock_format != "12");
    clockShowDate_ = (config.screensaver_clock_show_date == "true");
    clockShowSeconds_ = (config.screensaver_clock_seconds == "true");
    clockDateFormat_ = config.screensaver_clock_date_format;

    // If timeout changed to 0, deactivate immediately
    if (screensaverTimeout_ <= 0 && active_)
    {
        ESP_LOGI(TAG, "Timeout set to 0, deactivating screensaver");
        deactivate();
    }

    // If clock config changed, rebuild clock UI
    if (clockConfigChanged)
    {
        ESP_LOGI(TAG, "Clock config changed, rebuilding clock UI (mode=%s)", screensaverMode_.c_str());
        if (HAL::getInstance().getDisplay().tryLock(100))
        {
            rebuildClockUI();
            HAL::getInstance().getDisplay().unlock();
        }
    }

    // If active and dimming changed, apply immediately
    if (active_)
    {
        ESP_LOGD(TAG, "Screensaver active, updating dimming to %d", screensaverDimming_);
        HAL::getInstance().getDisplay().setBacklight(static_cast<uint8_t>(screensaverDimming_));
    }
    else
    {
        // Apply brightness immediately when screensaver is not active
        ESP_LOGD(TAG, "Screensaver inactive, applying brightness %d", normalBrightness_);
        HAL::getInstance().getDisplay().setBacklight(static_cast<uint8_t>(normalBrightness_));
    }
}

void ScreenSaver::updateClock()
{
    if (!timeLabel)
        return;

    if (!clockTimezone_.empty())
    {
        setenv("TZ", clockTimezone_.c_str(), 1);
        tzset();
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char timeBuf[32];
    if (clockUse24h_)
        std::strftime(timeBuf, sizeof(timeBuf), clockShowSeconds_ ? "%H:%M:%S" : "%H:%M", &timeinfo);
    else
        std::strftime(timeBuf, sizeof(timeBuf), clockShowSeconds_ ? "%I:%M:%S %p" : "%I:%M %p", &timeinfo);

    lv_label_set_text(timeLabel, timeBuf);

    if (clockShowDate_ && dateLabel)
    {
        std::string dateStr = formatDate(timeinfo);
        lv_label_set_text(dateLabel, dateStr.c_str());
    }
}

std::string ScreenSaver::formatDate(const struct tm& tm)
{
    char buf[64];

    if (clockDateFormat_ == "MM/DD/YYYY")
        std::strftime(buf, sizeof(buf), "%m/%d/%Y", &tm);
    else if (clockDateFormat_ == "YYYY-MM-DD")
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    else if (clockDateFormat_ == "DD MMM YYYY")
        std::strftime(buf, sizeof(buf), "%d %b %Y", &tm);
    else if (clockDateFormat_ == "MMM DD YYYY")
        std::strftime(buf, sizeof(buf), "%b %d %Y", &tm);
    else // Default: DD/MM/YYYY
        std::strftime(buf, sizeof(buf), "%d/%m/%Y", &tm);

    return buf;
}

#include "clock_widget.h"
#include "../theme.h"
#include "logging.h"
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>

static const char* TAG = "widget.clock";

// Font size lookup tables (sorted descending by line height for selection)
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
    { &roboto_bold_26, 26 },
    { &roboto_bold_24, 24 },
    { &roboto_bold_22, 22 },
};

static const FontEntry dateFonts[] = {
    { &roboto_regular_60, 60 },
    { &roboto_regular_48, 48 },
    { &roboto_regular_32, 32 },
    { &roboto_regular_28, 28 },
    { &roboto_regular_26, 26 },
    { &roboto_regular_24, 24 },
    { &roboto_regular_22, 22 },
};

ClockWidget::ClockWidget(lv_obj_t* parent,
                         const CalaosProtocol::WidgetConfig& config,
                         const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo)
{
    ESP_LOGI(TAG, "Creating clock widget at (%d,%d) size %dx%d",
             config.x, config.y, config.w, config.h);

    // Parse clock options from params
    auto it = config.params.find("clock_timezone");
    if (it != config.params.end())
        timezoneOffsetSeconds = parseTimezoneOffset(it->second);

    it = config.params.find("clock_format");
    if (it != config.params.end())
        use24hFormat = (it->second != "12");

    it = config.params.find("clock_show_date");
    if (it != config.params.end())
        showDate = (it->second == "true");

    it = config.params.find("clock_date_format");
    if (it != config.params.end())
        dateFormat = it->second;

    it = config.params.find("clock_seconds");
    if (it != config.params.end())
        showSeconds = (it->second == "true");

    ESP_LOGI(TAG, "Clock options: tz_offset=%ds, 24h=%d, show_date=%d, seconds=%d, date_fmt=%s",
             timezoneOffsetSeconds, use24hFormat, showDate, showSeconds, dateFormat.c_str());

    createUI();

    // Initial time display
    updateTime();

    // Start 1-second periodic timer
    updateTimer = LvglTimer::createRepeating(
        [this]()
        {
            updateTime();
        }, 1000);
}

void ClockWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    // No-op: Clock widget has no IO
    (void)state;
}

int ClockWidget::parseTimezoneOffset(const std::string& tz)
{
    if (tz.empty() || tz == "UTC")
        return 0;

    // Expected format: "UTC+N", "UTC-N", "UTC+N:MM", "UTC-N:MM"
    if (tz.size() < 4 || tz.substr(0, 3) != "UTC")
    {
        ESP_LOGW(TAG, "Invalid timezone format: %s, defaulting to UTC", tz.c_str());
        return 0;
    }

    char sign = tz[3];
    if (sign != '+' && sign != '-')
    {
        ESP_LOGW(TAG, "Invalid timezone sign in: %s, defaulting to UTC", tz.c_str());
        return 0;
    }

    std::string remainder = tz.substr(4);
    int hours = 0;
    int minutes = 0;

    size_t colonPos = remainder.find(':');
    if (colonPos != std::string::npos)
    {
        hours = std::atoi(remainder.substr(0, colonPos).c_str());
        minutes = std::atoi(remainder.substr(colonPos + 1).c_str());
    }
    else
    {
        hours = std::atoi(remainder.c_str());
    }

    int totalSeconds = (hours * 3600) + (minutes * 60);
    if (sign == '-')
        totalSeconds = -totalSeconds;

    ESP_LOGI(TAG, "Parsed timezone '%s' -> offset %d seconds", tz.c_str(), totalSeconds);
    return totalSeconds;
}

const lv_font_t* ClockWidget::selectFont(int availableHeight, int availableWidth,
                                         const char* referenceText,
                                         const FontEntry* fonts, int fontCount)
{
    for (int i = 0; i < fontCount; i++)
    {
        if (fonts[i].lineHeight > availableHeight)
            continue;

        // Measure text width with this font
        lv_point_t textSize;
        lv_txt_get_size(&textSize, referenceText, fonts[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        if (textSize.x <= availableWidth)
            return fonts[i].font;
    }
    return fonts[fontCount - 1].font;
}

void ClockWidget::createUI()
{
    // Container styling - same as other widgets
    setBgColor(theme_color_widget_bg_off);
    setBorderColor(theme_color_widget_border_off);
    setRadius(20);
    setBorderWidth(2);
    setPadding(16, 16, 16, 16);

    // Not clickable
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_SCROLLABLE);

    // Column flex layout, centered
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Calculate available space (widget size minus padding)
    int pixelW = config.w * gridInfo.cellWidth - 2 * gridInfo.padding;
    int pixelH = config.h * gridInfo.cellHeight - 2 * gridInfo.padding;
    int innerW = pixelW - 32; // 16px padding on each side
    int innerH = pixelH - 32;

    // Calculate font sizes based on available height
    int timeFontHeight;
    int dateFontHeight = 0;

    if (showDate)
    {
        // Reserve ~65% height for time, ~35% for date (with some gap)
        int gap = 4;
        timeFontHeight = (innerH * 65) / 100 - gap;
        dateFontHeight = (innerH * 35) / 100 - gap;
    }
    else
    {
        // All space for time
        timeFontHeight = innerH;
    }

    const char* timeRef;
    if (use24hFormat)
        timeRef = showSeconds ? "23:59:59" : "23:59";
    else
        timeRef = showSeconds ? "12:59:59 PM" : "12:59 PM";
    const lv_font_t* timeFont = selectFont(timeFontHeight, innerW, timeRef,
                                           timeFonts, sizeof(timeFonts) / sizeof(timeFonts[0]));
    ESP_LOGI(TAG, "Clock size %dx%d: inner=%dx%d, timeFontH=%d, selected font h=%d",
             config.w, config.h, innerW, innerH, timeFontHeight, timeFont->line_height);

    // Time label
    timeLabel = lv_label_create(get());
    lv_label_set_text(timeLabel, "--:--");
    lv_obj_set_style_text_font(timeLabel, timeFont, 0);
    lv_obj_set_style_text_color(timeLabel, theme_color_white, 0);
    lv_obj_set_style_text_align(timeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(timeLabel, innerW);
    lv_label_set_long_mode(timeLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Date label (optional)
    if (showDate)
    {
        const char* dateRef = "24/02/2026";
        const lv_font_t* dateFont = selectFont(dateFontHeight, innerW, dateRef,
                                               dateFonts, sizeof(dateFonts) / sizeof(dateFonts[0]));
        ESP_LOGI(TAG, "Clock date: dateFontH=%d, selected font h=%d",
                 dateFontHeight, dateFont->line_height);

        dateLabel = lv_label_create(get());
        lv_label_set_text(dateLabel, "");
        lv_obj_set_style_text_font(dateLabel, dateFont, 0);
        lv_obj_set_style_text_color(dateLabel, theme_color_blue, 0);
        lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(dateLabel, innerW);
        lv_label_set_long_mode(dateLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
}

std::string ClockWidget::formatDate(const struct tm& tm)
{
    char buf[64];

    if (dateFormat == "MM/DD/YYYY")
        std::strftime(buf, sizeof(buf), "%m/%d/%Y", &tm);
    else if (dateFormat == "YYYY-MM-DD")
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    else if (dateFormat == "DD MMM YYYY")
        std::strftime(buf, sizeof(buf), "%d %b %Y", &tm);
    else if (dateFormat == "MMM DD YYYY")
        std::strftime(buf, sizeof(buf), "%b %d %Y", &tm);
    else // Default: DD/MM/YYYY
        std::strftime(buf, sizeof(buf), "%d/%m/%Y", &tm);

    return buf;
}

void ClockWidget::updateTime()
{
    time_t now = time(nullptr);
    now += timezoneOffsetSeconds;

    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    // Format time
    char timeBuf[32];
    if (use24hFormat)
        std::strftime(timeBuf, sizeof(timeBuf), showSeconds ? "%H:%M:%S" : "%H:%M", &timeinfo);
    else
        std::strftime(timeBuf, sizeof(timeBuf), showSeconds ? "%I:%M:%S %p" : "%I:%M %p", &timeinfo);

    lv_label_set_text(timeLabel, timeBuf);

    // Update date if shown
    if (showDate && dateLabel)
    {
        std::string dateStr = formatDate(timeinfo);
        lv_label_set_text(dateLabel, dateStr.c_str());
    }
}

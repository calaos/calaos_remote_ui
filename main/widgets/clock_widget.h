#pragma once

#include "../calaos_widget.h"
#include "../calaos_protocol.h"
#include "../lvgl_timer.h"
#include "lvgl.h"
#include <string>
#include <memory>

/**
 * @brief Clock widget displaying time and optional date
 *
 * Supports all grid sizes (1x1 to 4x4) with adaptive font sizing.
 * Not linked to any IO - self-updates every second using LvglTimer.
 *
 * Configurable options via widget params:
 * - clock_timezone: POSIX TZ string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3")
 * - clock_format: "24" or "12"
 * - clock_show_date: "true" or "false"
 * - clock_date_format: "DD/MM/YYYY", "MM/DD/YYYY", "YYYY-MM-DD", "DD MMM YYYY", "MMM DD YYYY"
 * - clock_seconds: "true" or "false"
 */
class ClockWidget : public CalaosWidget
{
public:
    ClockWidget(lv_obj_t* parent,
                const CalaosProtocol::WidgetConfig& config,
                const GridLayoutInfo& gridInfo);

    ~ClockWidget() override = default;

protected:
    /**
     * @brief No-op for Clock (no IO state)
     */
    void onStateUpdate(const CalaosProtocol::IoState& state) override;

private:
    /**
     * @brief Create LVGL UI elements
     */
    void createUI();

    /**
     * @brief Update time/date display (called every second by timer)
     */
    void updateTime();

    /**
     * @brief Select the best font that fits both available height and width
     * @param availableHeight Available height in pixels
     * @param availableWidth Available width in pixels
     * @param referenceText Text to measure width against
     * @param fonts Font table to search
     * @param fontCount Number of entries in the font table
     * @return Pointer to the LVGL font
     */
    static const lv_font_t* selectFont(int availableHeight, int availableWidth,
                                       const char* referenceText,
                                       const struct FontEntry* fonts, int fontCount);

    /**
     * @brief Format date string from struct tm using configured date format
     * @param tm Time structure
     * @return Formatted date string
     */
    std::string formatDate(const struct tm& tm);

    // UI elements
    lv_obj_t* timeLabel = nullptr;
    lv_obj_t* dateLabel = nullptr;

    // Timer for periodic updates
    std::unique_ptr<LvglTimer> updateTimer;

    // Clock configuration
    std::string posixTz = "UTC0";
    bool use24hFormat = true;
    bool showDate = true;
    bool showSeconds = false;
    std::string dateFormat = "DD/MM/YYYY";
};

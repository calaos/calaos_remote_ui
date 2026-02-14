/**
 * @file ota_update_screen.h
 * @brief OTA update progress overlay screen
 */

#pragma once

#include "page_base.h"
#include "../flux/flux.h"
#include "version.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include <memory>
#include <atomic>
#include <string>
#include <mutex>

/**
 * @brief Full-screen overlay showing OTA update progress
 *
 * Displays:
 * - Title ("Firmware Update")
 * - Version being installed
 * - Progress bar (0-100%)
 * - Status text ("Downloading...", "Installing...", "Restarting...")
 */
class OtaUpdateScreen : public smooth_ui_toolkit::lvgl_cpp::Container
{
public:
    OtaUpdateScreen(lv_obj_t* parent);
    ~OtaUpdateScreen();

    /**
     * @brief Show the OTA screen with version info
     */
    void show(const std::string& version, const std::string& releaseNotes = "");

    /**
     * @brief Hide the OTA screen
     */
    void hide();

    /**
     * @brief Update progress display
     * @param percent Progress 0-100
     * @param status Status text
     */
    void updateProgress(int percent, const std::string& status);

    /**
     * @brief Show error state
     */
    void showError(const std::string& errorMessage);

    /**
     * @brief Process pending progress updates (must be called from LVGL thread with lock held)
     */
    void update();

private:
    void createUI();
    void onStateChanged(const AppState& state);
    void resetAfterError();
    static void errorTimerCallback(lv_timer_t* timer);

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> titleLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> versionLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> statusLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> currentVersionLabel;
    lv_obj_t* progressBar = nullptr;  // Raw LVGL bar - no C++ wrapper available
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> percentLabel;
    lv_timer_t* errorTimer = nullptr;

    SubscriptionId subscriptionId_ = 0;
    std::atomic<OtaStatus> lastStatus_{OtaStatus::Idle};

    // Thread-safe progress storage
    std::atomic<int> pendingProgress_{0};
    std::atomic<bool> progressNeedsUpdate_{false};
    std::atomic<bool> needsShow_{false};  // Set when "show:" command is pending, prevents progress from overwriting it
    std::mutex statusMutex_;
    std::string pendingStatusText_;
};

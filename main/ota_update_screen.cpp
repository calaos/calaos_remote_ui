/**
 * @file ota_update_screen.cpp
 * @brief OTA update progress overlay screen implementation
 */

#include "ota_update_screen.h"
#include "theme.h"
#include "logging.h"
#include "../flux/app_dispatcher.h"
#include "../hal/hal.h"
#include "board_config.h"
#include <cstring>

using namespace smooth_ui_toolkit;

static const char* TAG = "ota.screen";

OtaUpdateScreen::OtaUpdateScreen(lv_obj_t* parent):
    Container(parent)
{
    // Set up full screen overlay
    lv_obj_set_size(get(), BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT);
    lv_obj_center(get());
    lv_obj_set_style_bg_color(get(), lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(get(), LV_OPA_90, LV_PART_MAIN);

    createUI();

    // Subscribe to app state changes
    subscriptionId_ = AppStore::getInstance().subscribe(
        [this](const AppState& state)
        {
            onStateChanged(state);
        });

    // Initially hidden
    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);

    // Apply initial state in case OTA is already in progress
    const AppState& initialState = AppStore::getInstance().getState();
    if (initialState.ota.status != OtaStatus::Idle && initialState.ota.status != OtaStatus::Available)
    {
        ESP_LOGI(TAG, "OTA already in progress (status=%d), applying initial state", static_cast<int>(initialState.ota.status));
        lastStatus_.store(initialState.ota.status);
        pendingProgress_.store(initialState.ota.progress);
        if (initialState.ota.status == OtaStatus::Downloading)
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            pendingStatusText_ = "show:" + initialState.ota.version;
        }
        else if (initialState.ota.status == OtaStatus::Installing)
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            pendingStatusText_ = "Installing firmware...";
        }
        else if (initialState.ota.status == OtaStatus::Rebooting)
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            pendingStatusText_ = "Restarting device...";
        }
        else if (initialState.ota.status == OtaStatus::Error)
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            pendingStatusText_ = "error:" + initialState.ota.errorMessage;
        }
        progressNeedsUpdate_.store(true);
    }

    ESP_LOGI(TAG, "OTA update screen created");
}

OtaUpdateScreen::~OtaUpdateScreen()
{
    if (errorTimer)
    {
        lv_timer_del(errorTimer);
        errorTimer = nullptr;
    }
    if (subscriptionId_ != 0)
    {
        AppStore::getInstance().unsubscribe(subscriptionId_);
    }
}

void OtaUpdateScreen::createUI()
{
    // Title label
    titleLabel = std::make_unique<lvgl_cpp::Label>(*this);
    titleLabel->setText("Firmware Update");
    lv_obj_align(titleLabel->get(), LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_color(titleLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(titleLabel->get(), &roboto_bold_32, LV_PART_MAIN);

    // Version label
    versionLabel = std::make_unique<lvgl_cpp::Label>(*this);
    versionLabel->setText("v0.0.0");
    lv_obj_align(versionLabel->get(), LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_text_color(versionLabel->get(), theme_color_blue, LV_PART_MAIN);
    lv_obj_set_style_text_font(versionLabel->get(), &roboto_medium_28, LV_PART_MAIN);

    // Progress bar - use raw LVGL bar since there's no C++ wrapper
    progressBar = lv_bar_create(get());
    lv_obj_set_size(progressBar, 400, 30);
    lv_obj_align(progressBar, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(progressBar, 0, 100);
    lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progressBar, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progressBar, theme_color_blue, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progressBar, 15, LV_PART_MAIN);
    lv_obj_set_style_radius(progressBar, 15, LV_PART_INDICATOR);

    // Percent label (on the bar)
    percentLabel = std::make_unique<lvgl_cpp::Label>(*this);
    percentLabel->setText("0%");
    lv_obj_align(percentLabel->get(), LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_color(percentLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(percentLabel->get(), &roboto_bold_24, LV_PART_MAIN);

    // Status label
    statusLabel = std::make_unique<lvgl_cpp::Label>(*this);
    statusLabel->setText("Preparing update...");
    lv_obj_align(statusLabel->get(), LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_text_color(statusLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(statusLabel->get(), &roboto_light_26, LV_PART_MAIN);

    // Current version label at the bottom
    currentVersionLabel = std::make_unique<lvgl_cpp::Label>(*this);
    currentVersionLabel->setText("Current: " APP_VERSION);
    lv_obj_align(currentVersionLabel->get(), LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_text_color(currentVersionLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(currentVersionLabel->get(), &roboto_light_26, LV_PART_MAIN);
    lv_obj_set_style_opa(currentVersionLabel->get(), LV_OPA_50, LV_PART_MAIN);
}

void OtaUpdateScreen::show(const std::string& version, const std::string& releaseNotes)
{
    ESP_LOGI(TAG, "Showing OTA update screen for version %s", version.c_str());

    std::string versionText = "v" + version;
    versionLabel->setText(versionText.c_str());

    lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
    percentLabel->setText("0%");
    statusLabel->setText("Preparing update...");

    lv_obj_clear_flag(get(), LV_OBJ_FLAG_HIDDEN);
    // No need for move_foreground - we're on lv_layer_top() which is always above everything
}

void OtaUpdateScreen::hide()
{
    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);
}

void OtaUpdateScreen::updateProgress(int percent, const std::string& status)
{
    lv_bar_set_value(progressBar, percent, LV_ANIM_ON);

    char percentText[16];
    snprintf(percentText, sizeof(percentText), "%d%%", percent);
    percentLabel->setText(percentText);

    statusLabel->setText(status.c_str());
}

void OtaUpdateScreen::showError(const std::string& errorMessage)
{
    ESP_LOGE(TAG, "OTA error: %s", errorMessage.c_str());

    lv_obj_set_style_text_color(statusLabel->get(), theme_color_red, LV_PART_MAIN);
    statusLabel->setText(errorMessage.c_str());

    // Change progress bar color to red
    lv_obj_set_style_bg_color(progressBar, theme_color_red, LV_PART_INDICATOR);

    // Auto-hide after 5 seconds and reset OTA state
    if (errorTimer)
    {
        lv_timer_del(errorTimer);
    }
    errorTimer = lv_timer_create(errorTimerCallback, 5000, this);
    lv_timer_set_repeat_count(errorTimer, 1);
}

void OtaUpdateScreen::resetAfterError()
{
    // Reset OTA state to Idle so the app can function normally
    AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::OtaReset));

    // Reset UI colors for next time
    lv_obj_set_style_text_color(statusLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progressBar, theme_color_blue, LV_PART_INDICATOR);
}

void OtaUpdateScreen::onStateChanged(const AppState& state)
{
    // Store progress for timer to pick up (thread-safe)
    // Only process status changes that need immediate UI updates
    OtaStatus currentStatus = lastStatus_.load();
    bool statusChanged = (state.ota.status != currentStatus);

    // Handle progress updates for Downloading state (frequent, no status change)
    if (state.ota.status == OtaStatus::Downloading && !statusChanged)
    {
        pendingProgress_.store(state.ota.progress);
        // Only update status text if there is no pending "show:" command
        // Otherwise the progress event would overwrite the show command before
        // the LVGL thread has a chance to process it
        if (!needsShow_.load())
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            pendingStatusText_ = "Downloading firmware...";
        }
        progressNeedsUpdate_.store(true);
        return;
    }

    if (!statusChanged)
        return;

    lastStatus_.store(state.ota.status);

    // Store data for timer callback to process
    switch (state.ota.status)
    {
        case OtaStatus::Idle:
            needsShow_.store(false);
            pendingProgress_.store(0);
            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                pendingStatusText_ = "idle";
            }
            progressNeedsUpdate_.store(true);
            break;

        case OtaStatus::Available:
            // Update will auto-start, show screen when download begins
            break;

        case OtaStatus::Downloading:
            pendingProgress_.store(state.ota.progress);
            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                pendingStatusText_ = "show:" + state.ota.version;
            }
            needsShow_.store(true);
            progressNeedsUpdate_.store(true);
            break;

        case OtaStatus::Installing:
            needsShow_.store(false);
            pendingProgress_.store(100);
            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                pendingStatusText_ = "Installing firmware...";
            }
            progressNeedsUpdate_.store(true);
            break;

        case OtaStatus::Rebooting:
            needsShow_.store(false);
            pendingProgress_.store(100);
            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                pendingStatusText_ = "Restarting device...";
            }
            progressNeedsUpdate_.store(true);
            break;

        case OtaStatus::Error:
            needsShow_.store(false);
            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                pendingStatusText_ = "error:" + state.ota.errorMessage;
            }
            progressNeedsUpdate_.store(true);
            break;
    }
}

void OtaUpdateScreen::errorTimerCallback(lv_timer_t* timer)
{
    OtaUpdateScreen* self = static_cast<OtaUpdateScreen*>(lv_timer_get_user_data(timer));
    if (self)
    {
        self->hide();
        self->resetAfterError();
        self->errorTimer = nullptr;
    }
    lv_timer_del(timer);
}

void OtaUpdateScreen::update()
{
    if (!progressNeedsUpdate_.load())
    {
        return;
    }
    progressNeedsUpdate_.store(false);

    int progress = pendingProgress_.load();
    std::string statusText;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        statusText = pendingStatusText_;
    }

    // Handle special commands
    if (statusText == "idle")
    {
        hide();
        return;
    }

    if (statusText.rfind("show:", 0) == 0)
    {
        // Extract version and show
        std::string version = statusText.substr(5);
        show(version, "");
        needsShow_.store(false);
        updateProgress(progress, "Downloading firmware...");
        return;
    }

    if (statusText.rfind("error:", 0) == 0)
    {
        std::string errorMsg = statusText.substr(6);
        showError(errorMsg);
        return;
    }

    // Normal progress update
    updateProgress(progress, statusText);
}

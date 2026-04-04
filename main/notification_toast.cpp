#include "notification_toast.h"
#include "theme.h"
#include "logging.h"
#include "board_config.h"

static const char* TAG = "notification";

LV_FONT_DECLARE(roboto_bold_48);

NotificationToast::NotificationToast(lv_obj_t* parent):
    Container(parent)
{
    lv_obj_set_size(get(), BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT);
    lv_obj_align(get(), LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(get(), lv_color_make(20, 20, 20), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(get(), 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(get(), 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(get(), 40, LV_PART_MAIN);

    // Click to dismiss
    lv_obj_add_flag(get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(get(), clickCallback, LV_EVENT_CLICKED, this);

    createUI();

    subscriptionId_ = AppStore::getInstance().subscribe(
        [this](const AppState& state)
        {
            onStateChanged(state);
        });

    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);
}

NotificationToast::~NotificationToast()
{
    if (subscriptionId_)
        AppStore::getInstance().unsubscribe(subscriptionId_);

    if (hideTimer_)
    {
        lv_timer_del(hideTimer_);
        hideTimer_ = nullptr;
    }
}

void NotificationToast::createUI()
{
    // Scrollable container for long text
    lv_obj_set_flex_flow(get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(get(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(get(), LV_SCROLLBAR_MODE_AUTO);

    messageLabel_ = lv_label_create(get());
    lv_label_set_text(messageLabel_, "");
    lv_obj_set_width(messageLabel_, BOARD_DISPLAY_WIDTH - 80);
    lv_label_set_long_mode(messageLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(messageLabel_, theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(messageLabel_, &roboto_bold_48, LV_PART_MAIN);
    lv_obj_set_style_text_align(messageLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

void NotificationToast::show(const std::string& message)
{
    ESP_LOGI(TAG, "Showing notification: %s", message.c_str());

    lv_label_set_text(messageLabel_, message.c_str());
    lv_obj_clear_flag(get(), LV_OBJ_FLAG_HIDDEN);

    // Reset timer
    if (hideTimer_)
    {
        lv_timer_del(hideTimer_);
        hideTimer_ = nullptr;
    }

    hideTimer_ = lv_timer_create(timerCallback, 5 * 60 * 1000, this);
    lv_timer_set_repeat_count(hideTimer_, 1);
}

void NotificationToast::hide()
{
    lv_obj_add_flag(get(), LV_OBJ_FLAG_HIDDEN);

    if (hideTimer_)
    {
        lv_timer_del(hideTimer_);
        hideTimer_ = nullptr;
    }
}

void NotificationToast::timerCallback(lv_timer_t* timer)
{
    auto* self = static_cast<NotificationToast*>(lv_timer_get_user_data(timer));
    if (self)
    {
        self->hideTimer_ = nullptr;
        self->hide();
    }
}

void NotificationToast::clickCallback(lv_event_t* e)
{
    auto* self = static_cast<NotificationToast*>(lv_event_get_user_data(e));
    if (self)
        self->hide();
}

void NotificationToast::onStateChanged(const AppState& state)
{
    if (!state.notificationMessage.empty() && state.notificationVersion != lastNotificationVersion_)
    {
        lastNotificationVersion_ = state.notificationVersion;

        if (HAL::getInstance().getDisplay().tryLock(100))
        {
            show(state.notificationMessage);
            HAL::getInstance().getDisplay().unlock();
        }
    }
}

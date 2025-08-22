#include "startup_page.h"
#include "theme.h"
#include "images_generated.h"
#include "test_page.h"
#include "app_main.h"

using namespace smooth_ui_toolkit;

extern AppMain* g_appMain;

StartupPage::StartupPage(lv_obj_t *parent):
    PageBase(parent), lastNetworkState(false)
{
    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    logo = std::make_unique<lvgl_cpp::Image>(*this);
    logo->setSrc(&logo_full);

    // Network status label
    networkStatusLabel = std::make_unique<lvgl_cpp::Label>(*this);
    networkStatusLabel->setText("Initializing network...");
    networkStatusLabel->align(LV_ALIGN_BOTTOM_MID, 0, -120);
    lv_obj_set_style_text_color(networkStatusLabel->get(), lv_color_white(), LV_PART_MAIN);

    // Network status animation (pulsing effect)
    networkStatusAnimation.start = 128;
    networkStatusAnimation.end = 255;
    networkStatusAnimation.repeat = -1;  // Infinite repeat
    networkStatusAnimation.repeatType = smooth_ui_toolkit::animate_repeat_type::reverse;
    networkStatusAnimation.easingOptions().duration = 1.0f;
    networkStatusAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_in_out_quad;

    networkStatusAnimation.onUpdate([this](const float& value) {
        if (networkStatusLabel && !lastNetworkState) {
            lv_obj_set_style_opa(networkStatusLabel->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
        }
    });

    networkStatusAnimation.init();
    networkStatusAnimation.play();

    testButton = std::make_unique<lvgl_cpp::Button>(*this);
    testButton->setSize(LV_SIZE_CONTENT, 50);
    testButton->align(LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_add_event_cb(testButton->get(), testButtonCb, LV_EVENT_CLICKED, nullptr);
    testButton->label().setText("Test animations");

    initLogoAnimation();
}

void StartupPage::initLogoAnimation()
{
    // Now using lightweight easing animations - should work on both platforms
    logoDropAnimation.start = -getHeight();
    logoDropAnimation.end = 0;
    logoDropAnimation.delay = 0.2f;

    logoDropAnimation.easingOptions().duration = 0.6f;
    logoDropAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    logoDropAnimation.onUpdate([this](const float& value) {
        logo->align(LV_ALIGN_CENTER, 0, static_cast<int32_t>(value));
    });

    logoDropAnimation.init();
    logoDropAnimation.play();
}

void StartupPage::render()
{
    // Update logo animation
    logoDropAnimation.update();

    // Update network status
    updateNetworkStatus();

    // Update network status animation if still connecting
    if (!lastNetworkState) {
        networkStatusAnimation.update();
    }
}

void StartupPage::updateNetworkStatus()
{
    if (!g_appMain) return;

    bool networkReady = g_appMain->isNetworkReady();

    // Check for state change
    if (networkReady != lastNetworkState) {
        lastNetworkState = networkReady;

        if (networkReady) {
            // Network is now ready
            networkStatusLabel->setText("Network ready!");
            lv_obj_set_style_text_color(networkStatusLabel->get(), lv_color_hex(0x00FF00), LV_PART_MAIN);
            lv_obj_set_style_opa(networkStatusLabel->get(), LV_OPA_COVER, LV_PART_MAIN);  // Full opacity

            // Stop the pulsing animation
            networkStatusAnimation.cancel();
        }
    }
}

void StartupPage::testButtonCb(lv_event_t* e)
{
    static int testPageCount = 0;

    if (g_appMain && g_appMain->getStackView())
    {
        auto testPage = std::make_unique<TestPage>(lv_screen_active(),
            ("Test Page " + std::to_string(++testPageCount)).c_str());

        // Alternate between different animation types for testing
        stack_animation_type::Type_t animType;
        switch (testPageCount % 3)
        {
            case 1:
                animType = stack_animation_type::SlideVertical;
                break;
            case 2:
                animType = stack_animation_type::SlideHorizontal;
                break;
            default:
                animType = stack_animation_type::NoAnim;
                break;
        }

        g_appMain->getStackView()->push(std::move(testPage), animType);
    }
}

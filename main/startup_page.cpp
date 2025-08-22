#include "startup_page.h"
#include "theme.h"
#include "images_generated.h"
#include "test_page.h"
#include "app_main.h"

using namespace smooth_ui_toolkit;

extern AppMain* g_appMain;

StartupPage::StartupPage(lv_obj_t *parent):
    PageBase(parent)
{
    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    logo = std::make_unique<lvgl_cpp::Image>(*this);
    logo->setSrc(&logo_full);

    testButton = std::make_unique<lvgl_cpp::Button>(*this);
    testButton->setSize(150, 50);
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
    // Now using lightweight easing - safe to run on both platforms
    logoDropAnimation.update();
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

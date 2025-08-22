#include "test_page.h"
#include "theme.h"
#include "app_main.h"
#include <random>

using namespace smooth_ui_toolkit;

extern AppMain* g_appMain;

TestPage::TestPage(lv_obj_t *parent, const char* title):
    PageBase(parent)
{
    // Generate random color
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint8_t> colorDist(0, 255);

    lv_color_t randomColor = lv_color_make(colorDist(gen), colorDist(gen), colorDist(gen));
    setBgColor(randomColor);
    setBgOpa(LV_OPA_COVER);

    titleLabel = std::make_unique<lvgl_cpp::Label>(*this);
    titleLabel->setText(title);
    titleLabel->setAlign(LV_ALIGN_TOP_MID);
    titleLabel->setPos(0, 50);

    backButton = std::make_unique<lvgl_cpp::Button>(*this);
    backButton->setSize(LV_SIZE_CONTENT, 50);
    backButton->align(LV_ALIGN_BOTTOM_LEFT, 20, -20);
    backButton->label().setText("Back");
    lv_obj_add_event_cb(backButton->get(), backButtonCb, LV_EVENT_CLICKED, this);

    nextButton = std::make_unique<lvgl_cpp::Button>(*this);
    nextButton->setSize(LV_SIZE_CONTENT, 50);
    nextButton->align(LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    nextButton->label().setText("Next");
    lv_obj_add_event_cb(nextButton->get(), nextButtonCb, LV_EVENT_CLICKED, this);
}

void TestPage::render()
{
    // Nothing to animate here for now
}

void TestPage::backButtonCb(lv_event_t* e)
{
    if (g_appMain && g_appMain->getStackView())
    {
        // Test pop with animation
        static int popCount = 0;
        stack_animation_type::Type_t animType;
        switch ((++popCount) % 3)
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

        g_appMain->getStackView()->pop(animType);
    }
}

void TestPage::nextButtonCb(lv_event_t* e)
{
    if (g_appMain && g_appMain->getStackView())
    {
        static int nextPageCount = 0;
        auto testPage = std::make_unique<TestPage>(lv_screen_active(),
            ("Next Page " + std::to_string(++nextPageCount)).c_str());

        // Test push with animation
        stack_animation_type::Type_t animType;
        switch (nextPageCount % 3)
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
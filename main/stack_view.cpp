#include "stack_view.h"
#include <lvgl.h>
#include "logging.h"

using namespace smooth_ui_toolkit;

StackView::StackView(lv_obj_t *parent) :
    parentObj(parent),
    animating(false),
    animatingPage(nullptr),
    previousPage(nullptr),
    isPushAnimation(false),
    currentAnimType(stack_animation_type::NoAnim)
{
}

void StackView::push(std::unique_ptr<PageBase> page, stack_animation_type::Type_t animType)
{

    if (!page)
        return;

    if (animating)
    {
        return; // Don't allow new transitions while animating
    }

    PageBase* oldPage = currentPage();
    pageStack.push_back(std::move(page));
    PageBase* newPage = currentPage();

    if (animType == stack_animation_type::NoAnim)
    {
        hideAllPages();
        showCurrentPage();
    }
    else
    {
        startPushAnimation(newPage, oldPage, animType);
    }
}

void StackView::pop(stack_animation_type::Type_t animType)
{

    if (pageStack.empty())
    {
        return;
    }

    if (animating)
    {
        return; // Don't allow new transitions while animating
    }

    if (animType == stack_animation_type::NoAnim)
    {
        pageStack.pop_back();
        showCurrentPage();
    }
    else
    {
        // For animated pop, we need to keep the current page until animation completes
        PageBase* currentPagePtr = currentPage();
        PageBase* nextPage = pageStack.size() > 1 ? pageStack[pageStack.size() - 2].get() : nullptr;
        startPopAnimation(currentPagePtr, nextPage, animType);
    }
}

void StackView::clear()
{
    pageStack.clear();
}

bool StackView::empty() const
{
    return pageStack.empty();
}

size_t StackView::size() const
{
    return pageStack.size();
}

PageBase* StackView::currentPage() const
{
    if (pageStack.empty())
        return nullptr;

    return pageStack.back().get();
}

void StackView::render()
{
    if (animating)
    {
        slideAnimation.update();
        opacityAnimation.update();
    }

    PageBase* current = currentPage();
    if (current)
        current->render();
}

void StackView::hideAllPages()
{
    for (auto& page : pageStack)
    {
        if (page)
            lv_obj_add_flag(page->get(), LV_OBJ_FLAG_HIDDEN);
    }
}

void StackView::showCurrentPage()
{
    PageBase* current = currentPage();
    if (current)
        lv_obj_clear_flag(current->get(), LV_OBJ_FLAG_HIDDEN);
}

void StackView::startPushAnimation(PageBase* newPage, PageBase* oldPage, stack_animation_type::Type_t animType)
{
    animating = true;
    animatingPage = newPage;
    previousPage = oldPage;
    isPushAnimation = true;
    currentAnimType = animType;

    // Show both pages during animation
    if (oldPage)
        lv_obj_clear_flag(oldPage->get(), LV_OBJ_FLAG_HIDDEN);
    if (newPage)
        lv_obj_clear_flag(newPage->get(), LV_OBJ_FLAG_HIDDEN);

    switch (animType)
    {
        case stack_animation_type::SlideVertical:
            setupSlideVerticalPush(newPage, oldPage);
            break;
        case stack_animation_type::SlideHorizontal:
            setupSlideHorizontalPush(newPage, oldPage);
            break;
        default:
            break;
    }
}

void StackView::startPopAnimation(PageBase* currentPagePtr, PageBase* nextPage, stack_animation_type::Type_t animType)
{
    animating = true;
    animatingPage = currentPagePtr;
    previousPage = nextPage;
    isPushAnimation = false;
    currentAnimType = animType;

    // Show both pages during animation
    if (currentPagePtr)
        lv_obj_clear_flag(currentPagePtr->get(), LV_OBJ_FLAG_HIDDEN);
    if (nextPage)
        lv_obj_clear_flag(nextPage->get(), LV_OBJ_FLAG_HIDDEN);

    switch (animType)
    {
        case stack_animation_type::SlideVertical:
            setupSlideVerticalPop(currentPagePtr, nextPage);
            break;
        case stack_animation_type::SlideHorizontal:
            setupSlideHorizontalPop(currentPagePtr, nextPage);
            break;
        default:
            break;
    }
}

void StackView::setupSlideVerticalPush(PageBase* newPage, PageBase* oldPage)
{
    if (!newPage)
        return;

    // Initial position: new page starts 20px above
    lv_obj_set_pos(newPage->get(), 0, -20);
    lv_obj_set_style_opa(newPage->get(), LV_OPA_0, LV_PART_MAIN);

    // Slide animation: move from -20 to 0
    slideAnimation.start = -20;
    slideAnimation.end = 0;
    slideAnimation.easingOptions().duration = 0.3f;
    slideAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    slideAnimation.onUpdate([this, newPage](const float& value) {
        if (newPage)
            lv_obj_set_y(newPage->get(), static_cast<int32_t>(value));
    });

    // Opacity animation: fade from 0 to 100%
    opacityAnimation.start = 0;
    opacityAnimation.end = 255;
    opacityAnimation.easingOptions().duration = 0.3f;
    opacityAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    opacityAnimation.onUpdate([this, newPage](const float& value) {
        if (newPage)
            lv_obj_set_style_opa(newPage->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    opacityAnimation.onComplete([this]() {
        onAnimationComplete();
    });

    slideAnimation.init();
    opacityAnimation.init();
    slideAnimation.play();
    opacityAnimation.play();
}

void StackView::setupSlideVerticalPop(PageBase* currentPagePtr, PageBase* nextPage)
{
    if (!currentPagePtr)
        return;

    // Initial position: current page at 0
    lv_obj_set_pos(currentPagePtr->get(), 0, 0);
    lv_obj_set_style_opa(currentPagePtr->get(), LV_OPA_COVER, LV_PART_MAIN);

    // Slide animation: move from 0 to -20 (slide up)
    slideAnimation.start = 0;
    slideAnimation.end = -20;
    slideAnimation.easingOptions().duration = 0.3f;
    slideAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    slideAnimation.onUpdate([this, currentPagePtr](const float& value) {
        if (currentPagePtr)
            lv_obj_set_y(currentPagePtr->get(), static_cast<int32_t>(value));
    });

    // Opacity animation: fade from 100% to 0
    opacityAnimation.start = 255;
    opacityAnimation.end = 0;
    opacityAnimation.easingOptions().duration = 0.3f;
    opacityAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    opacityAnimation.onUpdate([this, currentPagePtr](const float& value) {
        if (currentPagePtr)
            lv_obj_set_style_opa(currentPagePtr->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    opacityAnimation.onComplete([this]() {
        onAnimationComplete();
    });

    slideAnimation.init();
    opacityAnimation.init();
    slideAnimation.play();
    opacityAnimation.play();
}

void StackView::setupSlideHorizontalPush(PageBase* newPage, PageBase* oldPage)
{
    if (!newPage)
        return;

    // Initial position: new page starts 20px from the left
    lv_obj_set_pos(newPage->get(), -20, 0);
    lv_obj_set_style_opa(newPage->get(), LV_OPA_0, LV_PART_MAIN);

    // Slide animation: move from -20 to 0
    slideAnimation.start = -20;
    slideAnimation.end = 0;
    slideAnimation.easingOptions().duration = 0.3f;
    slideAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    slideAnimation.onUpdate([this, newPage](const float& value) {
        if (newPage)
            lv_obj_set_x(newPage->get(), static_cast<int32_t>(value));
    });

    // Opacity animation: fade from 0 to 100%
    opacityAnimation.start = 0;
    opacityAnimation.end = 255;
    opacityAnimation.easingOptions().duration = 0.3f;
    opacityAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    opacityAnimation.onUpdate([this, newPage](const float& value) {
        if (newPage)
            lv_obj_set_style_opa(newPage->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    opacityAnimation.onComplete([this]() {
        onAnimationComplete();
    });

    slideAnimation.init();
    opacityAnimation.init();
    slideAnimation.play();
    opacityAnimation.play();
}

void StackView::setupSlideHorizontalPop(PageBase* currentPagePtr, PageBase* nextPage)
{
    if (!currentPagePtr)
        return;

    // Initial position: current page at 0
    lv_obj_set_pos(currentPagePtr->get(), 0, 0);
    lv_obj_set_style_opa(currentPagePtr->get(), LV_OPA_COVER, LV_PART_MAIN);

    // Slide animation: move from 0 to 20 (slide right)
    slideAnimation.start = 0;
    slideAnimation.end = 20;
    slideAnimation.easingOptions().duration = 0.3f;
    slideAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    slideAnimation.onUpdate([this, currentPagePtr](const float& value) {
        if (currentPagePtr)
            lv_obj_set_x(currentPagePtr->get(), static_cast<int32_t>(value));
    });

    // Opacity animation: fade from 100% to 0
    opacityAnimation.start = 255;
    opacityAnimation.end = 0;
    opacityAnimation.easingOptions().duration = 0.3f;
    opacityAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    opacityAnimation.onUpdate([this, currentPagePtr](const float& value) {
        if (currentPagePtr)
            lv_obj_set_style_opa(currentPagePtr->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    opacityAnimation.onComplete([this]() {
        onAnimationComplete();
    });

    slideAnimation.init();
    opacityAnimation.init();
    slideAnimation.play();
    opacityAnimation.play();
}

void StackView::onAnimationComplete()
{
    animating = false;

    // Clean up animation state BEFORE removing pages from stack
    if (animatingPage && animatingPage->get())
    {
        // Reset position and opacity
        lv_obj_set_pos(animatingPage->get(), 0, 0);
        lv_obj_set_style_opa(animatingPage->get(), LV_OPA_COVER, LV_PART_MAIN);
    }

    if (previousPage && previousPage->get())
    {
        // Reset position and opacity for previous page
        lv_obj_set_pos(previousPage->get(), 0, 0);
        lv_obj_set_style_opa(previousPage->get(), LV_OPA_COVER, LV_PART_MAIN);
    }

    // If this was a pop animation, remove the top page now (AFTER cleanup)
    if (!isPushAnimation && !pageStack.empty())
    {
        pageStack.pop_back();
    }

    // Hide all pages and show only current
    hideAllPages();
    showCurrentPage();

    // Reset animation pointers
    animatingPage = nullptr;
    previousPage = nullptr;
}
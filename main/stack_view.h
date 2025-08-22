#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include <vector>
#include <memory>

namespace stack_animation_type {
enum Type_t {
    NoAnim = 0,
    SlideVertical,    // Push: slide down from top, Pop: slide up to top
    SlideHorizontal   // Push: slide from left to right, Pop: slide from right to left
};
}

class StackView
{
public:
    StackView(lv_obj_t *parent);
    ~StackView() = default;

    void push(std::unique_ptr<PageBase> page, stack_animation_type::Type_t animType = stack_animation_type::NoAnim);
    void pop(stack_animation_type::Type_t animType = stack_animation_type::NoAnim);
    void clear();
    
    bool empty() const;
    size_t size() const;
    
    PageBase* currentPage() const;
    
    void render();

private:
    lv_obj_t *parentObj;
    std::vector<std::unique_ptr<PageBase>> pageStack;
    
    // Animation state
    bool animating;
    smooth_ui_toolkit::Animate slideAnimation;
    smooth_ui_toolkit::Animate opacityAnimation;
    PageBase* animatingPage;
    PageBase* previousPage;
    bool isPushAnimation;
    stack_animation_type::Type_t currentAnimType;
    
    void hideAllPages();
    void showCurrentPage();
    void startPushAnimation(PageBase* newPage, PageBase* oldPage, stack_animation_type::Type_t animType);
    void startPopAnimation(PageBase* currentPage, PageBase* nextPage, stack_animation_type::Type_t animType);
    void setupSlideVerticalPush(PageBase* newPage, PageBase* oldPage);
    void setupSlideVerticalPop(PageBase* currentPage, PageBase* nextPage);
    void setupSlideHorizontalPush(PageBase* newPage, PageBase* oldPage);
    void setupSlideHorizontalPop(PageBase* currentPage, PageBase* nextPage);
    void onAnimationComplete();
};
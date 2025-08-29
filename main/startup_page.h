#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "flux.h"
#include "calaos_discovery.h"
#include "lvgl_timer.h"
#include <memory>

class StartupPage: public PageBase
{
public:
    StartupPage(lv_obj_t *parent);
    void render() override;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> logo;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> networkStatusLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Spinner> networkSpinner;
    smooth_ui_toolkit::Animate logoDropAnimation;
    smooth_ui_toolkit::Animate networkStatusAnimation;

    // Provisioning UI elements
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> provisioningCodeBox;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> provisioningCodeLabel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> provisioningInstructionLabel;
    
    // Provisioning animations
    smooth_ui_toolkit::Animate logoMoveUpAnimation;
    smooth_ui_toolkit::Animate codeBoxAppearAnimation;
    smooth_ui_toolkit::Animate codeTextAppearAnimation;
    smooth_ui_toolkit::Animate instructionTextAppearAnimation;

    std::unique_ptr<CalaosDiscovery> calaosDiscovery;
    std::unique_ptr<LvglTimer> discoveryDelayTimer;

    void initLogoAnimation();
    void initProvisioningAnimations();
    void createProvisioningUI();
    void showProvisioningUI(const std::string& code);
    void hideProvisioningUI();
    void onStateChanged(const AppState& state);

    NetworkState lastNetworkState;
    CalaosServerState lastCalaosServerState;
    ProvisioningState lastProvisioningState;
};

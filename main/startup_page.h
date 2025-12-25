#pragma once

#include "page_base.h"
#include "smooth_ui_toolkit.h"
#include "lvgl/smooth_lvgl.h"
#include "flux.h"
#include "calaos_discovery.h"
#include "provisioning_requester.h"
#include "calaos_websocket_manager.h"
#include "lvgl_timer.h"
#include <memory>

class StartupPage: public PageBase
{
public:
    StartupPage(lv_obj_t *parent);
    ~StartupPage();
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

    // UI elements
    lv_obj_t* spinner = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* provCodeLabel = nullptr;
    lv_obj_t* provCodeValue = nullptr;
    lv_obj_t* provIpValue = nullptr;

    // Provisioning animations
    smooth_ui_toolkit::Animate logoMoveUpAnimation;
    smooth_ui_toolkit::Animate codeBoxAppearAnimation;
    smooth_ui_toolkit::Animate codeBoxFadeInAnimation;
    smooth_ui_toolkit::Animate codeTextAppearAnimation;
    smooth_ui_toolkit::Animate instructionTextAppearAnimation;

    std::unique_ptr<CalaosDiscovery> calaosDiscovery;
    std::unique_ptr<ProvisioningRequester> provisioningRequester;
    std::unique_ptr<CalaosWebSocketManager> calaosWebSocketManager;
    std::unique_ptr<LvglTimer> discoveryDelayTimer;

    void initLogoAnimation();
    void initProvisioningAnimations();
    void createProvisioningUI();
    void showProvisioningUI(const std::string& code);
    void hideProvisioningUI();
    void showVerifyingUI();
    void hideVerifyingUI();
    void onStateChanged(const AppState& state);

    NetworkState lastNetworkState;
    CalaosServerState lastCalaosServerState;
    ProvisioningState lastProvisioningState;
    CalaosWebSocketState lastWebSocketState;
    SubscriptionId subscriptionId_;
};

#include "startup_page.h"
#include "theme.h"
#include "images_generated.h"
#include "calaos_page.h"
#include "app_main.h"
#include "logging.h"
#include "../hal/hal.h"
#include "provisioning_manager.h"
#include "../flux/app_dispatcher.h"

using namespace smooth_ui_toolkit;

static const char* TAG = "StartupPage";
extern AppMain* g_appMain;

StartupPage::StartupPage(lv_obj_t *parent):
    PageBase(parent)
{
    // Initialize Calaos discovery and provisioning requester
    calaosDiscovery = std::make_unique<CalaosDiscovery>();
    provisioningRequester = std::make_unique<ProvisioningRequester>();
    setBgColor(theme_color_black);
    setBgOpa(LV_OPA_COVER);

    logo = std::make_unique<lvgl_cpp::Image>(*this);
    logo->setSrc(&logo_full);
    logo->align(LV_ALIGN_CENTER, 0, -720); // Start off-screen

    // Network spinner - positioned above the label
    networkSpinner = std::make_unique<lvgl_cpp::Spinner>(*this);
    networkSpinner->align(LV_ALIGN_BOTTOM_MID, 0, -180);
    lv_obj_set_size(networkSpinner->get(), 80, 80);
    lv_spinner_set_anim_params(networkSpinner->get(), 2000, 200);
    lv_obj_set_style_arc_color(networkSpinner->get(), theme_color_blue, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(networkSpinner->get(), theme_color_black, LV_PART_MAIN);

    // Network status label
    networkStatusLabel = std::make_unique<lvgl_cpp::Label>(*this);
    networkStatusLabel->setText("Initializing network...");
    networkStatusLabel->align(LV_ALIGN_BOTTOM_MID, 0, -120);
    networkStatusLabel->setTextFont(&lv_font_montserrat_26);

    lv_obj_set_style_text_color(networkStatusLabel->get(), lv_color_white(), LV_PART_MAIN);

    // Provisioning UI elements (initially hidden)
    createProvisioningUI();

    // Network status animation (pulsing effect)
    networkStatusAnimation.start = 128;
    networkStatusAnimation.end = 255;
    networkStatusAnimation.repeat = -1;  // Infinite repeat
    networkStatusAnimation.repeatType = smooth_ui_toolkit::animate_repeat_type::reverse;
    networkStatusAnimation.easingOptions().duration = 1.0f;
    networkStatusAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_in_out_quad;

    networkStatusAnimation.onUpdate([this](const float& value)
    {
        if (networkStatusLabel && !lastNetworkState.isReady && !lastProvisioningState.needsCodeDisplay())
            lv_obj_set_style_opa(networkStatusLabel->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    networkStatusAnimation.init();
    networkStatusAnimation.play();

    // Subscribe to state changes from AppStore
    AppStore::getInstance().subscribe([this](const AppState& state)
    {
        onStateChanged(state);
    });

    // Get initial state
    onStateChanged(AppStore::getInstance().getState());

    initLogoAnimation();
    initProvisioningAnimations();
}

StartupPage::~StartupPage()
{
    ESP_LOGI(TAG, "Destroying StartupPage");

    // Stop discovery and provisioning threads before destruction
    if (calaosDiscovery)
    {
        calaosDiscovery->stopDiscovery();
    }

    if (provisioningRequester)
    {
        provisioningRequester->stopRequesting();
    }

    ESP_LOGI(TAG, "StartupPage destroyed");
}

void StartupPage::initLogoAnimation()
{
    logoDropAnimation.start = -getHeight();
    logoDropAnimation.end = 0;
    logoDropAnimation.delay = 0.2f;

    logoDropAnimation.easingOptions().duration = 0.6f;
    logoDropAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    logoDropAnimation.onUpdate([this](const float& value)
    {
        logo->align(LV_ALIGN_CENTER, 0, static_cast<int32_t>(value));
    });

    logoDropAnimation.init();
    logoDropAnimation.play();
}

void StartupPage::createProvisioningUI()
{
    // Provisioning code box (large background box)
    provisioningCodeBox = std::make_unique<lvgl_cpp::Label>(*this);
    provisioningCodeBox->setText("");
    lv_obj_set_size(provisioningCodeBox->get(), 500, 220);
    lv_obj_align(provisioningCodeBox->get(), LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_opa(provisioningCodeBox->get(), LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(provisioningCodeBox->get(), theme_color_blue, LV_PART_MAIN);
    lv_obj_set_style_border_width(provisioningCodeBox->get(), 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(provisioningCodeBox->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_radius(provisioningCodeBox->get(), 15, LV_PART_MAIN);
    lv_obj_add_flag(provisioningCodeBox->get(), LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Provisioning code label (large text inside box)
    provisioningCodeLabel = std::make_unique<lvgl_cpp::Label>(*this);
    provisioningCodeLabel->setText("------");
    lv_obj_align(provisioningCodeLabel->get(), LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_text_color(provisioningCodeLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(provisioningCodeLabel->get(), &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_align(provisioningCodeLabel->get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(provisioningCodeLabel->get(), LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Provisioning instruction label
    provisioningInstructionLabel = std::make_unique<lvgl_cpp::Label>(*this);
    provisioningInstructionLabel->setText("Add this code in\nCalaos Installer");
    lv_obj_align(provisioningInstructionLabel->get(), LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_text_color(provisioningInstructionLabel->get(), theme_color_white, LV_PART_MAIN);
    lv_obj_set_style_text_font(provisioningInstructionLabel->get(), &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(provisioningInstructionLabel->get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(provisioningInstructionLabel->get(), LV_OBJ_FLAG_HIDDEN); // Initially hidden
}

void StartupPage::initProvisioningAnimations()
{
    // Logo move up animation (for when showing provisioning code)
    logoMoveUpAnimation.start = 0;
    logoMoveUpAnimation.end = -250;
    logoMoveUpAnimation.easingOptions().duration = 0.8f;
    logoMoveUpAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;

    logoMoveUpAnimation.onUpdate([this](const float& value)
    {
        if (logo)
            logo->align(LV_ALIGN_CENTER, 0, static_cast<int32_t>(value));
    });

    logoMoveUpAnimation.init();

    // Code box appear animation (slide up from bottom)
    codeBoxAppearAnimation.start = 200;  // Start below screen
    codeBoxAppearAnimation.end = -50;    // End position
    codeBoxAppearAnimation.delay = 0.4f; // Delay after logo animation
    codeBoxAppearAnimation.easingOptions().duration = 0.6f;
    codeBoxAppearAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_back;

    codeBoxAppearAnimation.onUpdate([this](const float& value)
    {
        if (provisioningCodeBox)
            lv_obj_align(provisioningCodeBox->get(), LV_ALIGN_CENTER, 0, static_cast<int32_t>(value));
    });

    codeBoxAppearAnimation.init();

    // Code box fade-in animation
    codeBoxFadeInAnimation.start = 0;
    codeBoxFadeInAnimation.end = 255;
    codeBoxFadeInAnimation.delay = 0.4f; // Same delay as slide animation
    codeBoxFadeInAnimation.easingOptions().duration = 0.6f;

    codeBoxFadeInAnimation.onUpdate([this](const float& value)
    {
        if (provisioningCodeBox)
            lv_obj_set_style_opa(provisioningCodeBox->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    codeBoxFadeInAnimation.init();

    // Code text appear animation (fade in)
    codeTextAppearAnimation.start = 0;
    codeTextAppearAnimation.end = 255;
    codeTextAppearAnimation.delay = 0.8f; // Delay after box animation
    codeTextAppearAnimation.easingOptions().duration = 0.4f;

    codeTextAppearAnimation.onUpdate([this](const float& value)
    {
        if (provisioningCodeLabel)
            lv_obj_set_style_opa(provisioningCodeLabel->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    codeTextAppearAnimation.init();

    // Instruction text appear animation (fade in)
    instructionTextAppearAnimation.start = 0;
    instructionTextAppearAnimation.end = 255;
    instructionTextAppearAnimation.delay = 1.0f; // Delay after code text
    instructionTextAppearAnimation.easingOptions().duration = 0.4f;

    instructionTextAppearAnimation.onUpdate([this](const float& value)
    {
        if (provisioningInstructionLabel)
            lv_obj_set_style_opa(provisioningInstructionLabel->get(), static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });

    instructionTextAppearAnimation.init();
}

void StartupPage::showProvisioningUI(const std::string& code)
{
    ESP_LOGI(TAG, "Showing provisioning UI with code: %s", code.c_str());

    // Update code text
    if (provisioningCodeLabel)
    {
        provisioningCodeLabel->setText(code.c_str());
    }

    // Show elements (initially transparent)
    if (provisioningCodeBox)
    {
        lv_obj_clear_flag(provisioningCodeBox->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(provisioningCodeBox->get(), LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (provisioningCodeLabel)
    {
        lv_obj_clear_flag(provisioningCodeLabel->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(provisioningCodeLabel->get(), LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (provisioningInstructionLabel)
    {
        lv_obj_clear_flag(provisioningInstructionLabel->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(provisioningInstructionLabel->get(), LV_OPA_TRANSP, LV_PART_MAIN);
    }

    // Hide network status elements
    if (networkStatusLabel)
    {
        lv_obj_add_flag(networkStatusLabel->get(), LV_OBJ_FLAG_HIDDEN);
    }
    if (networkSpinner)
    {
        lv_obj_add_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);
    }

    // Stop network animation
    networkStatusAnimation.cancel();

    // Start provisioning animations sequence
    logoMoveUpAnimation.play();
    codeBoxAppearAnimation.play();
    codeBoxFadeInAnimation.play();
    codeTextAppearAnimation.play();
    instructionTextAppearAnimation.play();
}

void StartupPage::hideProvisioningUI()
{
    ESP_LOGI(TAG, "Hiding provisioning UI");

    // Hide provisioning elements
    if (provisioningCodeBox)
    {
        lv_obj_add_flag(provisioningCodeBox->get(), LV_OBJ_FLAG_HIDDEN);
    }
    if (provisioningCodeLabel)
    {
        lv_obj_add_flag(provisioningCodeLabel->get(), LV_OBJ_FLAG_HIDDEN);
    }
    if (provisioningInstructionLabel)
    {
        lv_obj_add_flag(provisioningInstructionLabel->get(), LV_OBJ_FLAG_HIDDEN);
    }

    // Show network status elements
    if (networkStatusLabel)
    {
        lv_obj_clear_flag(networkStatusLabel->get(), LV_OBJ_FLAG_HIDDEN);
    }
    if (networkSpinner)
    {
        lv_obj_clear_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);
    }

    // Move logo back to center
    if (logo)
    {
        logo->align(LV_ALIGN_CENTER, 0, 0);
    }
}

void StartupPage::render()
{
    // Update logo animation
    logoDropAnimation.update();

    // Update provisioning animations
    logoMoveUpAnimation.update();
    codeBoxAppearAnimation.update();
    codeBoxFadeInAnimation.update();
    codeTextAppearAnimation.update();
    instructionTextAppearAnimation.update();

    // Update network status animation if still connecting or discovering (and not showing provisioning)
    if ((!lastNetworkState.isReady || lastCalaosServerState.isDiscovering) && !lastProvisioningState.needsCodeDisplay())
    {
        networkStatusAnimation.update();
    }
}

void StartupPage::onStateChanged(const AppState& state)
{
    // Check if application is shutting down to avoid deadlock
    if (AppDispatcher::getInstance().isStopping())
    {
        ESP_LOGD(TAG, "Ignoring state change - application is shutting down");
        return;
    }

    ESP_LOGD(TAG, "State changed - network isReady=%d, hasTimeout=%d, calaos isDiscovering=%d, hasServers=%d, provisioning status=%d",
             state.network.isReady, state.network.hasTimeout,
             state.calaosServer.isDiscovering, state.calaosServer.hasServers(),
             static_cast<int>(state.provisioning.status));

    // Lock LVGL display for thread-safe UI updates
    // Use a timeout loop to avoid deadlock during shutdown
    while (!HAL::getInstance().getDisplay().tryLock(100))
    {
        // Check if we should abort due to shutdown
        if (AppDispatcher::getInstance().isStopping())
        {
            ESP_LOGD(TAG, "Aborting state change - application is shutting down");
            return;
        }
    }

    // Check if network state has changed
    bool networkStateChanged = (state.network.isReady != lastNetworkState.isReady ||
                               state.network.hasTimeout != lastNetworkState.hasTimeout ||
                               state.network.ipAddress != lastNetworkState.ipAddress ||
                               state.network.connectionType != lastNetworkState.connectionType);

    // Check if Calaos server state has changed
    bool calaosStateChanged = (state.calaosServer.isDiscovering != lastCalaosServerState.isDiscovering ||
                              state.calaosServer.hasTimeout != lastCalaosServerState.hasTimeout ||
                              state.calaosServer.discoveredServers != lastCalaosServerState.discoveredServers);

    // Check if provisioning state has changed
    bool provisioningStateChanged = (state.provisioning.status != lastProvisioningState.status ||
                                   state.provisioning.provisioningCode != lastProvisioningState.provisioningCode ||
                                   state.provisioning.hasFailed != lastProvisioningState.hasFailed);

    if (networkStateChanged)
    {
        if (state.network.isReady && !lastNetworkState.isReady)
        {
            // Network just became ready
            // Initialize provisioning manager now that network is available
            ESP_LOGI(TAG, "Network ready, initializing provisioning manager");
            if (!getProvisioningManager().init())
            {
                ESP_LOGE(TAG, "Failed to initialize provisioning manager");
            }

            // Start discovery to find Calaos server
            ESP_LOGI(TAG, "Waiting 1 seconds before starting Calaos discovery");
            discoveryDelayTimer = LvglTimer::createOneShot([this]()
            {
                ESP_LOGI(TAG, "Starting Calaos discovery after 1-second delay");
                calaosDiscovery->startDiscovery();
            }, 1000);
        }
        else if (state.network.hasTimeout)
        {
            // Network timeout - show error message
            networkStatusLabel->setText("Network connection failed\nPlease connect WiFi or Ethernet\nand restart the device");
            lv_obj_set_style_text_color(networkStatusLabel->get(), theme_color_red, LV_PART_MAIN);
            lv_obj_set_style_opa(networkStatusLabel->get(), LV_OPA_COVER, LV_PART_MAIN);
            networkStatusLabel->setTextFont(&lv_font_montserrat_26);

            // Hide the spinner on timeout
            lv_obj_add_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);

            // Stop the pulsing animation
            networkStatusAnimation.cancel();
        }
        else if (!state.network.isReady)
        {
            // Network still connecting
            networkStatusLabel->setText("Initializing network...");
            lv_obj_set_style_text_color(networkStatusLabel->get(), theme_color_white, LV_PART_MAIN);

            // Show the spinner
            lv_obj_clear_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Handle Calaos discovery state changes
    if (calaosStateChanged)
    {
        if (state.calaosServer.isDiscovering)
        {
            // Show discovery in progress
            networkStatusLabel->setText("Searching for Calaos Server");
            lv_obj_set_style_text_color(networkStatusLabel->get(), theme_color_white, LV_PART_MAIN);
            lv_obj_set_style_opa(networkStatusLabel->get(), LV_OPA_COVER, LV_PART_MAIN);

            // Show the spinner
            lv_obj_clear_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);

            // Restart pulsing animation if not already running
            if (networkStatusAnimation.currentPlayingState() != animate_state::playing)
                networkStatusAnimation.play();
        }
        else if (state.calaosServer.hasServers())
        {
            // Server found - show success
            std::string serverInfo = "Calaos Server found:\n" + state.calaosServer.selectedServer;
            if (state.calaosServer.discoveredServers.size() > 1)
            {
                serverInfo += "\n(" + std::to_string(state.calaosServer.discoveredServers.size()) + " servers found)";
            }

            networkStatusLabel->setText(serverInfo.c_str());
            lv_obj_set_style_text_color(networkStatusLabel->get(), theme_color_white, LV_PART_MAIN);
            lv_obj_set_style_opa(networkStatusLabel->get(), LV_OPA_COVER, LV_PART_MAIN);

            // Hide the spinner when server found
            lv_obj_add_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);

            // Stop the pulsing animation
            networkStatusAnimation.cancel();

            ESP_LOGI(TAG, "Calaos server found, checking provisioning status");

            // If showing provisioning code, start provisioning requests
            // Use a deferred callback to avoid holding display lock during HTTP client init
            if (state.provisioning.needsCodeDisplay() && !state.provisioning.provisioningCode.empty())
            {
                ESP_LOGI(TAG, "Scheduling provisioning requests to server: %s with code: %s",
                        state.calaosServer.selectedServer.c_str(),
                        state.provisioning.provisioningCode.c_str());

                if (!provisioningRequester->isRequesting())
                {
                    std::string serverIp = state.calaosServer.selectedServer;
                    std::string code = state.provisioning.provisioningCode;

                    // Start requester in a deferred callback (after display unlock)
                    LvglTimer::createOneShot([this, serverIp, code]()
                    {
                        ESP_LOGI(TAG, "Starting provisioning requests (deferred)");
                        provisioningRequester->startRequesting(serverIp, code);
                    }, 10);  // 10ms delay
                }
            }
        }
        else if (state.calaosServer.hasTimeout)
        {
            // Discovery timeout - show error message
            networkStatusLabel->setText("No Calaos Server found\nPlease check your network\nand try again");
            lv_obj_set_style_text_color(networkStatusLabel->get(), theme_color_red, LV_PART_MAIN);
            lv_obj_set_style_opa(networkStatusLabel->get(), LV_OPA_COVER, LV_PART_MAIN);

            // Hide the spinner on timeout
            lv_obj_add_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);

            // Stop the pulsing animation
            networkStatusAnimation.cancel();
        }
    }

    // Handle provisioning state changes
    if (provisioningStateChanged)
    {
        switch (state.provisioning.status)
        {
            case ProvisioningStatus::ShowingCode:
            {
                // Show provisioning code UI with animations
                if (!state.provisioning.provisioningCode.empty())
                {
                    showProvisioningUI(state.provisioning.provisioningCode);

                    // If server already found, start provisioning requests
                    // Use a deferred callback to avoid holding display lock during HTTP client init
                    if (state.calaosServer.hasServers())
                    {
                        ESP_LOGI(TAG, "Scheduling provisioning requests to server: %s with code: %s",
                                state.calaosServer.selectedServer.c_str(),
                                state.provisioning.provisioningCode.c_str());

                        if (!provisioningRequester->isRequesting())
                        {
                            std::string serverIp = state.calaosServer.selectedServer;
                            std::string code = state.provisioning.provisioningCode;

                            // Start requester in a deferred callback (after display unlock)
                            LvglTimer::createOneShot([this, serverIp, code]()
                            {
                                ESP_LOGI(TAG, "Starting provisioning requests (deferred)");
                                provisioningRequester->startRequesting(serverIp, code);
                            }, 10);  // 10ms delay
                        }
                    }
                }
                break;
            }

            case ProvisioningStatus::Provisioned:
            {
                // Stop provisioning requests if still running
                if (provisioningRequester->isRequesting())
                {
                    ESP_LOGI(TAG, "Stopping provisioning requests - device provisioned");
                    provisioningRequester->stopRequesting();
                }

                // Hide provisioning UI and continue normal flow
                hideProvisioningUI();

                // Connect WebSocket if not already connected
                if (!calaosWebSocketManager)
                {
                    ESP_LOGI(TAG, "Creating WebSocket manager");
                    LvglTimer::createOneShot([this]()
                    {
                        calaosWebSocketManager = std::make_unique<CalaosWebSocketManager>();
                        if (calaosWebSocketManager->connect())
                        {
                            ESP_LOGI(TAG, "WebSocket connection initiated");
                            networkStatusLabel->setText("Connecting to Calaos server...");
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Failed to initiate WebSocket connection");
                            networkStatusLabel->setText("Connection failed");
                        }
                    }, 1000);
                }
                break;
            }

            case ProvisioningStatus::NotProvisioned:
            default:
            {
                // Ensure provisioning UI is hidden in case we're transitioning back
                hideProvisioningUI();

                // Stop provisioning requests if running
                if (provisioningRequester->isRequesting())
                {
                    provisioningRequester->stopRequesting();
                }
                break;
            }
        }

        if (state.provisioning.hasFailed)
        {
            ESP_LOGW(TAG, "Provisioning failed - could show error message");
            // Could add error handling UI here
        }
    }

    // Handle WebSocket state changes
    if (state.websocket.isConnected != lastWebSocketState.isConnected)
    {
        if (state.websocket.isConnected)
        {
            ESP_LOGI(TAG, "WebSocket connected successfully");
            networkStatusLabel->setText("Connected to Calaos!");

            // Hide spinner after connection
            LvglTimer::createOneShot([this]()
            {
                if (networkSpinner)
                    lv_obj_add_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);
                if (networkStatusLabel)
                    lv_obj_add_flag(networkStatusLabel->get(), LV_OBJ_FLAG_HIDDEN);

                // Push CalaosPage after successful connection
                if (g_appMain && g_appMain->getStackView())
                {
                    ESP_LOGI(TAG, "Pushing CalaosPage");
                    auto calaosPage = std::make_unique<CalaosPage>(lv_screen_active());
                    g_appMain->getStackView()->push(std::move(calaosPage),
                                                   stack_animation_type::SlideVertical);
                }
            }, 800);
        }
        else if (state.websocket.isConnecting)
        {
            networkStatusLabel->setText("Connecting...");
        }
    }

    // Handle WebSocket authentication failure - return to provisioning
    if (state.websocket.authFailed && !lastWebSocketState.authFailed)
    {
        ESP_LOGE(TAG, "WebSocket authentication failed - resetting provisioning");

        // Disconnect WebSocket
        if (calaosWebSocketManager)
        {
            calaosWebSocketManager->disconnect();
            calaosWebSocketManager.reset();
        }

        // Reset provisioning
        LvglTimer::createOneShot([this]()
        {
            ProvisioningManager& provMgr = getProvisioningManager();
            provMgr.resetProvisioning();

            // Show error message
            networkStatusLabel->setText("Authentication failed - please reprovision");
            lv_obj_clear_flag(networkStatusLabel->get(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(networkSpinner->get(), LV_OBJ_FLAG_HIDDEN);

            // Restart discovery after a delay
            LvglTimer::createOneShot([this]()
            {
                ESP_LOGI(TAG, "Restarting discovery after auth failure");
                calaosDiscovery->startDiscovery();
            }, 3000);
        }, 100);
    }

    // Update cached states
    lastNetworkState = state.network;
    lastCalaosServerState = state.calaosServer;
    lastProvisioningState = state.provisioning;
    lastWebSocketState = state.websocket;

    // Unlock LVGL display
    HAL::getInstance().getDisplay().unlock();
}

// void StartupPage::testButtonCb(lv_event_t* e)
// {
//     static int testPageCount = 0;

//     if (g_appMain && g_appMain->getStackView())
//     {
//         auto testPage = std::make_unique<TestPage>(lv_screen_active(),
//             ("Test Page " + std::to_string(++testPageCount)).c_str());

//         // Alternate between different animation types for testing
//         stack_animation_type::Type_t animType;
//         switch (testPageCount % 3)
//         {
//             case 1:
//                 animType = stack_animation_type::SlideVertical;
//                 break;
//             case 2:
//                 animType = stack_animation_type::SlideHorizontal;
//                 break;
//             default:
//                 animType = stack_animation_type::NoAnim;
//                 break;
//         }

//         g_appMain->getStackView()->push(std::move(testPage), animType);
//     }
// }

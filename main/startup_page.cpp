#include "startup_page.h"
#include "theme.h"
#include "images_generated.h"
#include "test_page.h"
#include "app_main.h"
#include "logging.h"
#include "../hal/hal.h"

using namespace smooth_ui_toolkit;

static const char* TAG = "StartupPage";

StartupPage::StartupPage(lv_obj_t *parent):
    PageBase(parent)
{
    // Initialize Calaos discovery
    calaosDiscovery = std::make_unique<CalaosDiscovery>();
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
    lv_obj_set_style_text_color(networkStatusLabel->get(), lv_color_white(), LV_PART_MAIN);

    // Network status animation (pulsing effect)
    networkStatusAnimation.start = 128;
    networkStatusAnimation.end = 255;
    networkStatusAnimation.repeat = -1;  // Infinite repeat
    networkStatusAnimation.repeatType = smooth_ui_toolkit::animate_repeat_type::reverse;
    networkStatusAnimation.easingOptions().duration = 1.0f;
    networkStatusAnimation.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_in_out_quad;

    networkStatusAnimation.onUpdate([this](const float& value)
    {
        if (networkStatusLabel && !lastNetworkState.isReady)
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

void StartupPage::render()
{
    // Update logo animation
    logoDropAnimation.update();

    // Update network status animation if still connecting or discovering
    if (!lastNetworkState.isReady || lastCalaosServerState.isDiscovering)
    {
        networkStatusAnimation.update();
    }
}

void StartupPage::onStateChanged(const AppState& state)
{
    ESP_LOGD(TAG, "State changed - network isReady=%d, hasTimeout=%d, calaos isDiscovering=%d, hasServers=%d",
             state.network.isReady, state.network.hasTimeout,
             state.calaosServer.isDiscovering, state.calaosServer.hasServers());

    // Lock LVGL display for thread-safe UI updates
    HAL::getInstance().getDisplay().lock(0);

    // Check if network state has changed
    bool networkStateChanged = (state.network.isReady != lastNetworkState.isReady ||
                               state.network.hasTimeout != lastNetworkState.hasTimeout ||
                               state.network.ipAddress != lastNetworkState.ipAddress ||
                               state.network.connectionType != lastNetworkState.connectionType);

    // Check if Calaos server state has changed
    bool calaosStateChanged = (state.calaosServer.isDiscovering != lastCalaosServerState.isDiscovering ||
                              state.calaosServer.hasTimeout != lastCalaosServerState.hasTimeout ||
                              state.calaosServer.discoveredServers != lastCalaosServerState.discoveredServers);

    if (networkStateChanged)
    {
        if (state.network.isReady && !lastNetworkState.isReady)
        {
            // Network just became ready - wait 2 seconds before starting Calaos discovery
            ESP_LOGI(TAG, "Network ready, waiting 2 seconds before starting Calaos discovery");
            discoveryDelayTimer = LvglTimer::createOneShot([this]() {
                ESP_LOGI(TAG, "Starting Calaos discovery after 2-second delay");
                calaosDiscovery->startDiscovery();
            }, 2000);
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
            {
                networkStatusAnimation.play();
            }
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

    // Update cached states
    lastNetworkState = state.network;
    lastCalaosServerState = state.calaosServer;

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

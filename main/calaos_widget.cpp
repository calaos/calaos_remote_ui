#include "calaos_widget.h"
#include "calaos_websocket_manager.h"
#include "hal.h"
#include "logging.h"

static const char* TAG = "widget";

CalaosWidget::CalaosWidget(lv_obj_t* parent,
                           const CalaosProtocol::WidgetConfig& config,
                           const GridLayoutInfo& gridInfo):
    Container(parent),
    config(config),
    gridInfo(gridInfo),
    subscriptionId_(0)
{
    ESP_LOGI(TAG, "Creating widget: type=%s, io_id=%s, pos=(%d,%d), size=(%dx%d)",
            config.type.c_str(), config.io_id.c_str(),
            config.x, config.y, config.w, config.h);

    // Calculate and apply grid-based position
    calculateAndApplyPosition();

    // Set default styling
    setBgOpa(LV_OPA_COVER);
    setPadding(0, 0, 0, 0);

    // Subscribe to state changes
    subscribeToStateChanges();

    // Try to get initial state from AppStore
    const AppState& state = AppStore::getInstance().getState();
    auto it = state.ioStates.find(config.io_id);
    if (it != state.ioStates.end())
    {
        currentState = it->second;
        ESP_LOGI(TAG, "Widget %s found initial state: %s",
                config.io_id.c_str(), currentState.state.c_str());
    }
    else
    {
        ESP_LOGW(TAG, "Widget %s: IO state not found in AppStore", config.io_id.c_str());
        // Set default state
        currentState.id = config.io_id;
        currentState.type = config.type;
        currentState.state = "unknown";
        currentState.name = config.io_id;
    }
}

CalaosWidget::~CalaosWidget()
{
    ESP_LOGI(TAG, "Destroying widget: %s", config.io_id.c_str());

    // Unsubscribe from AppStore to prevent dangling pointer
    AppStore::getInstance().unsubscribe(subscriptionId_);
}

void CalaosWidget::calculateAndApplyPosition()
{
    // Calculate pixel position from grid coordinates
    int pixelX = config.x * gridInfo.cellWidth + gridInfo.padding;
    int pixelY = config.y * gridInfo.cellHeight + gridInfo.padding;
    int pixelW = config.w * gridInfo.cellWidth - 2 * gridInfo.padding;
    int pixelH = config.h * gridInfo.cellHeight - 2 * gridInfo.padding;

    ESP_LOGI(TAG, "Widget %s grid pos (%d,%d) size (%dx%d) -> pixel pos (%d,%d) size (%dx%d)",
            config.io_id.c_str(),
            config.x, config.y, config.w, config.h,
            pixelX, pixelY, pixelW, pixelH);

    // Apply position and size
    setPos(pixelX, pixelY);
    setSize(pixelW, pixelH);
}

void CalaosWidget::subscribeToStateChanges()
{
    // Subscribe to AppStore state changes
    subscriptionId_ = AppStore::getInstance().subscribe([this](const AppState& appState)
    {
        onAppStateChanged(appState);
    });
}

void CalaosWidget::onAppStateChanged(const AppState& appState)
{
    // Find our IO state in the map
    auto it = appState.ioStates.find(config.io_id);
    if (it == appState.ioStates.end())
    {
        // IO state not present (yet)
        return;
    }

    const CalaosProtocol::IoState& newState = it->second;

    // Check if state actually changed
    if (newState.state == currentState.state &&
        newState.name == currentState.name &&
        newState.enabled == currentState.enabled &&
        newState.visible == currentState.visible)
    {
        // No change
        return;
    }

    // Update current state
    currentState = newState;

    ESP_LOGI(TAG, "Widget %s state update: %s", config.io_id.c_str(), newState.state.c_str());

    // Acquire display lock before updating UI
    if (HAL::getInstance().getDisplay().tryLock(100))
    {
        try
        {
            // Call child implementation to update UI
            onStateUpdate(newState);
        }
        catch (const std::exception& e)
        {
            ESP_LOGE(TAG, "Exception in onStateUpdate for %s: %s",
                    config.io_id.c_str(), e.what());
        }

        HAL::getInstance().getDisplay().unlock();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire display lock for widget %s update", config.io_id.c_str());
    }
}

bool CalaosWidget::sendStateChange(const std::string& newState)
{
    ESP_LOGI(TAG, "Widget %s sending state change: %s", config.io_id.c_str(), newState.c_str());

    // Access global WebSocket manager
    if (!g_wsManager)
    {
        ESP_LOGW(TAG, "WebSocket manager not available - state change not sent");
        return false;
    }

    if (!g_wsManager->isConnected())
    {
        ESP_LOGW(TAG, "WebSocket not connected - state change not sent");
        return false;
    }

    // Send state change via WebSocket
    return g_wsManager->setIoState(config.io_id, newState);
}

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

    // Only subscribe to IO state changes if io_id is set (non-IO widgets like Clock skip this)
    if (!config.io_id.empty())
    {
        // Subscribe to state changes
        subscribeToStateChanges();

        // Try to get initial state from AppStore
        const AppState& state = AppStore::getInstance().getState();
        auto refusals = state.ioCommandRefusals.find(config.io_id);
        if (refusals != state.ioCommandRefusals.end())
            refusalsSeen_ = refusals->second;

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
            currentState = placeholderState();
        }
    }
    else
    {
        ESP_LOGI(TAG, "Widget type=%s has no io_id, skipping IO state subscription", config.type.c_str());
    }
}

const std::string& CalaosWidget::getDisplayName() const
{
    if (!config.override_name.empty())
        return config.override_name;
    if (!currentState.name.empty())
        return currentState.name;
    return config.io_id;
}

const std::string& CalaosWidget::getDisplayName(const CalaosProtocol::IoState& state) const
{
    if (!config.override_name.empty())
        return config.override_name;
    if (!state.name.empty())
        return state.name;
    return config.io_id;
}

CalaosWidget::~CalaosWidget()
{
    ESP_LOGI(TAG, "Destroying widget: type=%s io_id=%s", config.type.c_str(), config.io_id.c_str());

    // Unsubscribe from AppStore to prevent dangling pointer
    if (subscriptionId_ != 0)
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

CalaosProtocol::IoState CalaosWidget::placeholderState() const
{
    CalaosProtocol::IoState placeholder;
    placeholder.id = config.io_id;
    placeholder.type = config.type;
    placeholder.state = "unknown";
    placeholder.name = config.io_id;
    return placeholder;
}

void CalaosWidget::onAppStateChanged(const AppState& appState)
{
    uint32_t refusals = 0;
    auto r = appState.ioCommandRefusals.find(config.io_id);
    if (r != appState.ioCommandRefusals.end())
        refusals = r->second;
    const bool refused = refusals != refusalsSeen_;

    // An IO dropped by a config update must not keep showing its last state
    auto it = appState.ioStates.find(config.io_id);
    const CalaosProtocol::IoState newState = it != appState.ioStates.end() ? it->second : placeholderState();
    const bool changed = newState != currentState;

    if (!changed && !refused)
        return;

    ESP_LOGI(TAG, "Widget %s state update: %s", config.io_id.c_str(), newState.state.c_str());

    // Acquire display lock before updating UI
    if (HAL::getInstance().getDisplay().tryLock(100))
    {
        // Only once the UI can follow: a failed lock must leave the change detectable
        try
        {
            if (refused)
            {
                refusalsSeen_ = refusals;
                onCommandRefused();
            }
            if (changed)
            {
                currentState = newState;
                onStateUpdate(newState);
            }
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

bool CalaosWidget::sendDeviceCommand(const nlohmann::json& command)
{
    if (!g_wsManager)
    {
        ESP_LOGW(TAG, "WebSocket manager not available - device command not sent");
        return false;
    }

    return g_wsManager->sendDeviceCommand(config.io_id, command);
}

bool CalaosWidget::sendDeviceMode(const std::string& mode)
{
    if (!g_wsManager)
    {
        ESP_LOGW(TAG, "WebSocket manager not available - device mode not sent");
        return false;
    }

    return g_wsManager->sendDeviceMode(config.io_id, mode);
}

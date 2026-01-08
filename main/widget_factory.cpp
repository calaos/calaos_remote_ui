#include "widget_factory.h"
#include "widgets/widget_error.h"
#include "widgets/light_switch_widget.h"
#include "widgets/temperature_widget.h"
#include "widgets/scenario_widget.h"
#include "logging.h"
#include <sstream>

static const char* TAG = "factory";

WidgetFactory& WidgetFactory::getInstance()
{
    static WidgetFactory instance;
    return instance;
}

WidgetFactory::WidgetFactory()
{
    ESP_LOGI(TAG, "Initializing WidgetFactory");
    registerBuiltinWidgets();
}

void WidgetFactory::registerWidget(const std::string& typeName,
                                  int width,
                                  int height,
                                  WidgetCreator creator)
{
    std::string key = makeKey(typeName, width, height);
    creators[key] = creator;
    ESP_LOGI(TAG, "Registered widget: %s", key.c_str());
}

bool WidgetFactory::isRegistered(const std::string& typeName, int width, int height) const
{
    std::string key = makeKey(typeName, width, height);
    return creators.find(key) != creators.end();
}

std::string WidgetFactory::makeKey(const std::string& type, int w, int h) const
{
    std::ostringstream oss;
    oss << type << "_" << w << "x" << h;
    return oss.str();
}

std::unique_ptr<CalaosWidget> WidgetFactory::createWidget(
    lv_obj_t* parent,
    const CalaosProtocol::WidgetConfig& config,
    const GridLayoutInfo& gridInfo)
{
    std::string key = makeKey(config.type, config.w, config.h);

    ESP_LOGI(TAG, "Creating widget: %s (io_id=%s)", key.c_str(), config.io_id.c_str());

    // Look up creator
    auto it = creators.find(key);
    if (it != creators.end())
    {
        // Found - create the widget
        return it->second(parent, config, gridInfo);
    }

    // Not found - create WidgetError
    ESP_LOGW(TAG, "Widget type/size not supported: %s - creating WidgetError", key.c_str());

    std::string errorMsg = "Unsupported: " + key;
    return std::make_unique<WidgetError>(parent, config, gridInfo, errorMsg);
}

void WidgetFactory::registerBuiltinWidgets()
{
    ESP_LOGI(TAG, "Registering built-in widgets");

    // Register LightSwitch 1x1
    registerWidget("LightSwitch", 1, 1,
        [](lv_obj_t* parent, const auto& config, const auto& gridInfo) {
            return std::make_unique<LightSwitchWidget>(parent, config, gridInfo);
        }
    );

    // Register Temperature 1x1
    registerWidget("Temperature", 1, 1,
        [](lv_obj_t* parent, const auto& config, const auto& gridInfo) {
            return std::make_unique<TemperatureWidget>(parent, config, gridInfo);
        }
    );

    // Register Scenario 1x1
    registerWidget("Scenario", 1, 1,
        [](lv_obj_t* parent, const auto& config, const auto& gridInfo) {
            return std::make_unique<ScenarioWidget>(parent, config, gridInfo);
        }
    );

    ESP_LOGI(TAG, "Built-in widgets registered: %zu", creators.size());
}

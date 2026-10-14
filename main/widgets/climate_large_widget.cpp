#include "climate_large_widget.h"
#include "../theme.h"
#include "climate_ui.h"
#include "logging.h"

static const char* TAG = "widget.climate.large";

using namespace climate;

namespace
{
constexpr int kPad = 12;
}

ClimateLargeWidget::ClimateLargeWidget(lv_obj_t* parent,
                                       const CalaosProtocol::WidgetConfig& config,
                                       const GridLayoutInfo& gridInfo):
    CalaosWidget(parent, config, gridInfo)
{
    ESP_LOGI(TAG, "Creating large climate widget: %s (size %dx%d)", config.io_id.c_str(), config.w, config.h);

    setRadius(20);
    setBorderWidth(2);
    setPadding(kPad, kPad, kPad, kPad);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(get(), LV_OBJ_FLAG_CLICKABLE);

    const int contentW = config.w * gridInfo.cellWidth - 2 * gridInfo.padding - 2 * kPad - 4;
    const int contentH = config.h * gridInfo.cellHeight - 2 * gridInfo.padding - 2 * kPad - 4;

    ClimateDetail::Host host;
    host.send = [this](const Command& cmd)
    {
        return cmd.isPlainMode() ? sendDeviceMode(cmd.mode) : sendDeviceCommand(cmd.partial);
    };
    host.styled = [this](const Status& st)
    {
        const climate_ui::Surface surface = climate_ui::surfaceFor(st.backdrop);
        setBgColor(surface.bg);
        setBorderColor(surface.border);
    };

    panel_ = std::make_unique<ClimateDetail>(get(), contentW, contentH, getDisplayName(), host);

    reader_.read(currentState);
    panel_->update(reader_.caps(), reader_.values(), reader_.present());
}

ClimateLargeWidget::~ClimateLargeWidget()
{
    // A page rebuilt mid-edit must not lose the user's last taps
    panel_.reset();
}

void ClimateLargeWidget::onStateUpdate(const CalaosProtocol::IoState& state)
{
    reader_.read(state);
    panel_->setTitle(getDisplayName());
    panel_->update(reader_.caps(), reader_.values(), reader_.present());
}

void ClimateLargeWidget::onCommandRefused()
{
    panel_->refused();
}

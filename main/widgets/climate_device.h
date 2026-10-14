#pragma once

#include "../calaos_protocol.h"
#include "climate_logic.h"
#include <string>

namespace climate
{

// A widget's reading of the device in its IO state. Capabilities rarely change
// while the state does at every io_changed, so they are parsed again only when
// their text does.
class DeviceReader
{
public:
    void read(const CalaosProtocol::IoState& state)
    {
        present_ = state.device.has_value();
        if (!present_)
        {
            caps_ = Capabilities{};
            values_ = Values{};
            return;
        }

        if (state.device->capabilities != capsText_ || state.device->ios != iosText_)
        {
            capsText_ = state.device->capabilities;
            iosText_ = state.device->ios;
            caps_ = Capabilities::parse(capsText_, iosText_);
        }
        values_ = Values::parse(state.device->state, state.state);
    }

    bool present() const { return present_; }
    const Capabilities& caps() const { return caps_; }
    const Values& values() const { return values_; }

private:
    bool present_ = false;
    Capabilities caps_;
    Values values_;
    std::string capsText_;
    std::string iosText_;
};

} // namespace climate

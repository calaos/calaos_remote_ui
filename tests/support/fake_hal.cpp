#include "fake_hal.h"
#include "logging.h"

uint64_t FakeHal::nowMs = 0;

namespace
{

class FakeDisplay : public HalDisplay
{
public:
    HalResult init() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }
    DisplayInfo getDisplayInfo() const override { return {BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT, BOARD_DISPLAY_COLOR_DEPTH}; }
    HalResult setBacklight(uint8_t) override { return HalResult::OK; }
    HalResult backlightOn() override { return HalResult::OK; }
    HalResult backlightOff() override { return HalResult::OK; }
    void lock(uint32_t) override {}
    bool tryLock(uint32_t) override { return true; }
    void unlock() override {}
    lv_display_t* getLvglDisplay() override { return nullptr; }
};

class FakeInput : public HalInput
{
public:
    HalResult init() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }
    lv_indev_t* getLvglInputDevice() override { return nullptr; }
};

class FakeNetwork : public HalNetwork
{
public:
    HalResult init() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }
    HalResult scanWifi(std::vector<WifiConfig>&) override { return HalResult::OK; }
    HalResult connectWifi(const WifiConfig&) override { return HalResult::OK; }
    HalResult disconnectWifi() override { return HalResult::OK; }
    WifiStatus getWifiStatus() const override { return WifiStatus::DISCONNECTED; }
    HalResult registerWifiCallback(WifiEventCallback) override { return HalResult::OK; }
    std::string getLocalIp() const override { return "127.0.0.1"; }
    std::string getMacAddress() const override { return "00:00:00:00:00:00"; }
    void retryConnection() override {}
};

class FakeSystem : public HalSystem
{
public:
    HalResult init() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }
    void delay(uint32_t) override {}
    uint64_t getTimeMs() override { return FakeHal::nowMs; }
    void restart() override {}
    std::string getDeviceInfo() const override { return "host-test"; }
    HalResult saveConfig(const std::string&, const std::string&) override { return HalResult::ERROR; }
    HalResult loadConfig(const std::string&, std::string&) override { return HalResult::ERROR; }
    HalResult eraseConfig(const std::string&) override { return HalResult::OK; }
    HalResult loadDeviceConfig(DeviceConfig&) override { return HalResult::ERROR; }
    HalResult saveDeviceConfig(const CalaosConfig&) override { return HalResult::ERROR; }
    HalResult initNtp() override { return HalResult::OK; }
    HalResult waitForTimeSync(uint32_t) override { return HalResult::OK; }
    bool isTimeSynced() const override { return true; }
    void startNtpRetryTimer() override {}
    void stopNtpRetryTimer() override {}
};

class FakeRelay : public HalRelay
{
public:
    HalResult init() override { return HalResult::OK; }
    HalResult deinit() override { return HalResult::OK; }
    int getRelayCount() const override { return 0; }
    HalResult setRelay(int, bool) override { return HalResult::OK; }
    bool getRelayState(int) const override { return false; }
};

FakeDisplay g_display;
FakeInput g_input;
FakeNetwork g_network;
FakeSystem g_system;
FakeRelay g_relay;

} // namespace

HAL& HAL::getInstance()
{
    return FakeHal::instance();
}

HAL::HAL() = default;

void HAL::initLogger()
{
}

FakeHal& FakeHal::instance()
{
    static FakeHal hal;
    return hal;
}

HalDisplay& FakeHal::getDisplay() { return g_display; }
HalInput& FakeHal::getInput() { return g_input; }
HalNetwork& FakeHal::getNetwork() { return g_network; }
HalSystem& FakeHal::getSystem() { return g_system; }
HalRelay& FakeHal::getRelay() { return g_relay; }

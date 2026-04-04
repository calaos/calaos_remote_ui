#include "linux_hal_relay.h"
#include "board_config.h"
#include "logging.h"

#if BOARD_RELAY_COUNT > 0
#include <fstream>
#include <string>
#endif

static const char* TAG = "LinuxHalRelay";

HalResult LinuxHalRelay::init()
{
#if BOARD_RELAY_COUNT > 0
    static const int gpioPins[] = { BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO };

    for (int i = 0; i < BOARD_RELAY_COUNT; i++)
    {
        int pin = gpioPins[i];
        if (pin < 0)
        {
            ESP_LOGW(TAG, "Relay %d has invalid GPIO pin %d, skipping", i + 1, pin);
            continue;
        }

        // Export GPIO via sysfs
        std::ofstream exportFile("/sys/class/gpio/export");
        if (exportFile.is_open())
        {
            exportFile << pin;
            exportFile.close();
        }

        // Set direction to output
        std::string dirPath = "/sys/class/gpio/gpio" + std::to_string(pin) + "/direction";
        std::ofstream dirFile(dirPath);
        if (dirFile.is_open())
        {
            dirFile << "out";
            dirFile.close();
        }
        else
        {
            ESP_LOGW(TAG, "Failed to set GPIO %d direction, relay %d may not work", pin, i + 1);
        }

        relayStates_[i] = false;
        ESP_LOGI(TAG, "Relay %d initialized on GPIO %d (sysfs)", i + 1, pin);
    }
#else
    ESP_LOGI(TAG, "No relays configured for this board");
#endif

    return HalResult::OK;
}

HalResult LinuxHalRelay::deinit()
{
#if BOARD_RELAY_COUNT > 0
    static const int gpioPins[] = { BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO };

    for (int i = 0; i < BOARD_RELAY_COUNT; i++)
    {
        int pin = gpioPins[i];
        if (pin < 0)
            continue;

        std::ofstream unexportFile("/sys/class/gpio/unexport");
        if (unexportFile.is_open())
        {
            unexportFile << pin;
            unexportFile.close();
        }
    }
#endif

    return HalResult::OK;
}

HalResult LinuxHalRelay::setRelay(int relay, bool state)
{
    if (relay < 1 || relay > BOARD_RELAY_COUNT)
    {
        ESP_LOGW(TAG, "Invalid relay number: %d (max: %d)", relay, BOARD_RELAY_COUNT);
        return HalResult::ERROR;
    }

#if BOARD_RELAY_COUNT > 0
    static const int gpioPins[] = { BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO };
    int pin = gpioPins[relay - 1];

    if (pin < 0)
    {
        ESP_LOGW(TAG, "Relay %d has invalid GPIO pin, simulating only", relay);
        relayStates_[relay - 1] = state;
        return HalResult::OK;
    }

    std::string valuePath = "/sys/class/gpio/gpio" + std::to_string(pin) + "/value";
    std::ofstream valueFile(valuePath);
    if (valueFile.is_open())
    {
        valueFile << (state ? "1" : "0");
        valueFile.close();
        relayStates_[relay - 1] = state;
        ESP_LOGI(TAG, "Relay %d set to %s (GPIO %d)", relay, state ? "ON" : "OFF", pin);
        return HalResult::OK;
    }

    ESP_LOGE(TAG, "Failed to write GPIO %d for relay %d", pin, relay);
    return HalResult::ERROR;
#else
    ESP_LOGW(TAG, "No relays on this board, ignoring setRelay(%d, %s)", relay, state ? "ON" : "OFF");
    return HalResult::NOT_SUPPORTED;
#endif
}

bool LinuxHalRelay::getRelayState(int relay) const
{
    if (relay < 1 || relay > BOARD_RELAY_COUNT)
        return false;

#if BOARD_RELAY_COUNT > 0
    return relayStates_[relay - 1];
#else
    return false;
#endif
}

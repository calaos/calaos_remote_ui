#include "esp32_hal_relay.h"
#include "board_config.h"
#include "logging.h"

#if BOARD_RELAY_COUNT > 0
#include "driver/gpio.h"
#endif

static const char* TAG = "Esp32HalRelay";

HalResult Esp32HalRelay::init()
{
#if BOARD_RELAY_COUNT > 0
    static const int gpioPins[] = { BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO };

    for (int i = 0; i < BOARD_RELAY_COUNT; i++)
    {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << gpioPins[i]);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure GPIO %d for relay %d: %s",
                     gpioPins[i], i + 1, esp_err_to_name(ret));
            return HalResult::ERROR;
        }

        gpio_set_level(static_cast<gpio_num_t>(gpioPins[i]), 0);
        relayStates_[i] = false;
        ESP_LOGI(TAG, "Relay %d initialized on GPIO %d", i + 1, gpioPins[i]);
    }
#else
    ESP_LOGI(TAG, "No relays configured for this board");
#endif

    return HalResult::OK;
}

HalResult Esp32HalRelay::deinit()
{
    return HalResult::OK;
}

int Esp32HalRelay::getGpioPin(int relay) const
{
#if BOARD_RELAY_COUNT > 0
    static const int gpioPins[] = { BOARD_RELAY_1_GPIO, BOARD_RELAY_2_GPIO };
    if (relay >= 1 && relay <= BOARD_RELAY_COUNT)
        return gpioPins[relay - 1];
#endif
    return -1;
}

HalResult Esp32HalRelay::setRelay(int relay, bool state)
{
    if (relay < 1 || relay > BOARD_RELAY_COUNT)
    {
        ESP_LOGW(TAG, "Invalid relay number: %d (max: %d)", relay, BOARD_RELAY_COUNT);
        return HalResult::ERROR;
    }

#if BOARD_RELAY_COUNT > 0
    int pin = getGpioPin(relay);
    if (pin < 0)
        return HalResult::ERROR;

    esp_err_t ret = gpio_set_level(static_cast<gpio_num_t>(pin), state ? 1 : 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set relay %d (GPIO %d): %s",
                 relay, pin, esp_err_to_name(ret));
        return HalResult::ERROR;
    }

    relayStates_[relay - 1] = state;
    ESP_LOGI(TAG, "Relay %d set to %s (GPIO %d)", relay, state ? "ON" : "OFF", pin);
#endif

    return HalResult::OK;
}

bool Esp32HalRelay::getRelayState(int relay) const
{
    if (relay < 1 || relay > BOARD_RELAY_COUNT)
        return false;

#if BOARD_RELAY_COUNT > 0
    return relayStates_[relay - 1];
#else
    return false;
#endif
}

#include "linux_hal_input.h"
#include "logging.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <thread>
#include <chrono>

static const char* TAG = "hal.input";

void LinuxHalInput::lvgl_input_read(lv_indev_t* indev, lv_indev_data_t* data)
{
    LinuxHalInput* input = static_cast<LinuxHalInput*>(lv_indev_get_user_data(indev));
    if (!input) return;

    TouchData touch_data;
    if (input->readTouch(touch_data) == HalResult::OK)
    {
        data->point.x = touch_data.x;
        data->point.y = touch_data.y;
        data->state = touch_data.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

HalResult LinuxHalInput::init()
{
    try
    {
        ESP_LOGI(TAG, "Initializing Linux touch input");

        // Try to open touch device (common paths)
        const char* touch_devices[] = {
            "/dev/input/event0",
            "/dev/input/event1",
            "/dev/input/event2",
            "/dev/input/touchscreen",
            nullptr
        };

        for (int i = 0; touch_devices[i] != nullptr; i++)
        {
            touch_fd_ = open(touch_devices[i], O_RDONLY | O_NONBLOCK);
            if (touch_fd_ != -1)
            {
                ESP_LOGI(TAG, "Opened touch device: %s", touch_devices[i].c_str());
                break;
            }
        }

        if (touch_fd_ == -1)
        {
            ESP_LOGI(TAG, "No touch device found, using mouse simulation");
            // Continue anyway, we can simulate with mouse or keyboard
        }

        // Create LVGL input device
        input_device_ = lv_indev_create();
        if (!input_device_)
        {
            ESP_LOGE(TAG, "Failed to create LVGL input device");
            if (touch_fd_ != -1) close(touch_fd_);
            return HalResult::ERROR;
        }

        lv_indev_set_type(input_device_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(input_device_, lvgl_input_read);
        lv_indev_set_user_data(input_device_, this);

        // Start touch reading thread
        thread_running_ = true;
        touch_thread_ = std::thread(&LinuxHalInput::touchThreadFunction, this);

        ESP_LOGI(TAG, "Linux input initialized");
        return HalResult::OK;
    }
    catch (...)
    {
        ESP_LOGE(TAG, "Exception during Linux input init");
        return HalResult::ERROR;
    }
}

HalResult LinuxHalInput::deinit()
{
    thread_running_ = false;

    if (touch_thread_.joinable())
        touch_thread_.join();

    if (input_device_)
    {
        lv_indev_delete(input_device_);
        input_device_ = nullptr;
    }

    if (touch_fd_ != -1)
    {
        close(touch_fd_);
        touch_fd_ = -1;
    }

    return HalResult::OK;
}

HalResult LinuxHalInput::registerTouchCallback(TouchEventCallback callback)
{
    touch_callback_ = callback;
    return HalResult::OK;
}

HalResult LinuxHalInput::readTouch(TouchData& touch_data)
{
    touch_data = last_touch_data_;
    return HalResult::OK;
}

lv_indev_t* LinuxHalInput::getLvglInputDevice()
{
    return input_device_;
}

void LinuxHalInput::touchThreadFunction()
{
    struct input_event ev;
    TouchData current_touch = {};

    while (thread_running_)
    {
        if (touch_fd_ == -1)
        {
            // No touch device, sleep and continue
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ssize_t bytes = read(touch_fd_, &ev, sizeof(struct input_event));
        if (bytes == sizeof(struct input_event))
        {
            switch (ev.type)
            {
                case EV_ABS:
                    switch (ev.code)
                    {
                        case ABS_X:
                            current_touch.x = ev.value;
                            break;
                        case ABS_Y:
                            current_touch.y = ev.value;
                            break;
                    }
                    break;

                case EV_KEY:
                    if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)
                    {
                        current_touch.pressed = (ev.value == 1);
                    }
                    break;

                case EV_SYN:
                    if (ev.code == SYN_REPORT)
                    {
                        last_touch_data_ = current_touch;
                        if (touch_callback_ && current_touch.pressed)
                        {
                            touch_callback_(current_touch);
                        }
                    }
                    break;
            }
        }
        else
        {
            // No data available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
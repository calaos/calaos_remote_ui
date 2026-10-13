#include <gtest/gtest.h>
#include "logging.h"
#include "support/headless_lvgl.h"

int main(int argc, char** argv)
{
    esp_log_level_set("*", ESP_LOG_NONE);
    headless::init();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

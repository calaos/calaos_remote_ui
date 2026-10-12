#include <gtest/gtest.h>
#include "logging.h"

int main(int argc, char** argv)
{
    esp_log_level_set("*", ESP_LOG_NONE);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

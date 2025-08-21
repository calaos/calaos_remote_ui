#include "app_main.h"

extern "C" void app_main(void)
{
    AppMain app;
    if (app.init())
        app.run();
}

#ifndef ESP_PLATFORM
int main()
{
    app_main();
    return 0;
}
#endif

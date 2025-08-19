#include "app_main.h"

#ifdef ESP_PLATFORM
extern "C" void app_main(void)
{
    AppMain app;
    if (app.init())
        app.run();
}
#else
int main()
{
    AppMain app;
    if (app.init())
        app.run();
    return 0;
}
#endif

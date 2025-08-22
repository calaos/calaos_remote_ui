#include "app_main.h"
#ifndef ESP_PLATFORM
#include "linux/display_backend_selector.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <signal.h>
#endif

static AppMain* app = nullptr;

extern "C" void app_main(void)
{
    app = new AppMain();
    if (app->initFast())  // Use fast initialization for better UX
        app->run();

    delete app;
    app = nullptr;
}

#ifndef ESP_PLATFORM

void signalHandler(int signal)
{
    if (app)
    {
        std::cout << "\nShutdown signal received, exiting gracefully..." << std::endl;
        app->stop();
    }
}

void printUsage(const char* progName)
{
    std::cout << "Usage: " << progName << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --display-backend <backend>  Force specific display backend\n";
    std::cout << "  --input-backend <backend>    Force specific input backend\n";
    std::cout << "  --list-backends             List available backends\n";
    std::cout << "  --help                      Show this help message\n";
    std::cout << "\nSupported display backends: fbdev, drm, sdl, x11, gles\n";
    std::cout << "Supported input backends: evdev, libinput\n";
    std::cout << "\nEnvironment variables:\n";
    std::cout << "  CALAOS_DISPLAY_BACKEND      Override display backend\n";
    std::cout << "  CALAOS_INPUT_BACKEND        Override input backend\n";
    std::cout << "  LV_LINUX_FBDEV_DEVICE       Override framebuffer device path\n";
    std::cout << "  LV_LINUX_DRM_CARD           Override DRM card path\n";
    std::cout << "  LV_LINUX_EVDEV_POINTER_DEVICE Override evdev input device path\n";
}

int main(int argc, char* argv[])
{
    DisplayBackendSelector& selector = DisplayBackendSelector::getInstance();

    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--list-backends") == 0)
        {
            std::cout << "Available backends:\n";
            std::cout << "\nDisplay backends:\n";
            selector.listAvailableBackends();
            std::cout << "\nInput backends:\n";
            // List available input backends
            std::vector<std::string> inputBackends = {"evdev", "libinput"};
            for (const auto& backend : inputBackends)
            {
                std::cout << "  - " << backend << std::endl;
            }
            return 0;
        }
        else if (strcmp(argv[i], "--display-backend") == 0)
        {
            if (i + 1 < argc)
            {
                selector.setBackendOverride(argv[++i]);
            }
            else
            {
                std::cerr << "Error: --display-backend requires a backend name\n";
                return 1;
            }
        }
        else if (strcmp(argv[i], "--input-backend") == 0)
        {
            if (i + 1 < argc)
            {
                setenv("CALAOS_INPUT_BACKEND", argv[++i], 1);
            }
            else
            {
                std::cerr << "Error: --input-backend requires a backend name\n";
                return 1;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    app_main();
    return 0;
}
#endif

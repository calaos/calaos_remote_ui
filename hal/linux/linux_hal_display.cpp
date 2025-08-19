#include "linux_hal_display.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <chrono>
#include <cstdlib>

static void linuxFbFlush(lv_display_t* disp, const lv_area_t* area, lv_color_t* colorP)
{
    LinuxHalDisplay* display = static_cast<LinuxHalDisplay*>(lv_display_get_user_data(disp));
    if (!display) 
        return;
}

HalResult LinuxHalDisplay::init()
{
    std::cout << "Initializing Linux framebuffer display" << std::endl;
    
    fbFd = open("/dev/fb0", O_RDWR);
    if (fbFd == -1)
    {
        std::cerr << "Failed to open framebuffer device" << std::endl;
        return HalResult::ERROR;
    }
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &finfo) == -1)
    {
        std::cerr << "Failed to get fixed screen info" << std::endl;
        close(fbFd);
        return HalResult::ERROR;
    }
    
    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &vinfo) == -1)
    {
        std::cerr << "Failed to get variable screen info" << std::endl;
        close(fbFd);
        return HalResult::ERROR;
    }
    
    displayInfo.width = 720;
    displayInfo.height = 720;
    displayInfo.colorDepth = 16;
    
    fbSize = displayInfo.width * displayInfo.height * (displayInfo.colorDepth / 8);
    
    fbBuffer = static_cast<uint8_t*>(mmap(0, fbSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0));
    if (fbBuffer == MAP_FAILED)
    {
        std::cerr << "Failed to map framebuffer" << std::endl;
        close(fbFd);
        return HalResult::ERROR;
    }
    
    display = lv_display_create(displayInfo.width, displayInfo.height);
    if (!display)
    {
        std::cerr << "Failed to create LVGL display" << std::endl;
        munmap(fbBuffer, fbSize);
        close(fbFd);
        return HalResult::ERROR;
    }
    
    size_t bufSize = displayInfo.width * 50;
    void* buf1 = malloc(bufSize * sizeof(lv_color_t));
    void* buf2 = malloc(bufSize * sizeof(lv_color_t));
    
    if (!buf1 || !buf2)
    {
        std::cerr << "Failed to allocate display buffers" << std::endl;
        free(buf1);
        free(buf2);
        lv_display_delete(display);
        munmap(fbBuffer, fbSize);
        close(fbFd);
        return HalResult::ERROR;
    }
    
    lv_display_set_buffers(display, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, linuxFbFlush);
    lv_display_set_user_data(display, this);
    
    std::cout << "Linux display initialized: " << displayInfo.width 
              << "x" << displayInfo.height << ", " << displayInfo.colorDepth << "-bit" << std::endl;
    
    return HalResult::OK;
}

HalResult LinuxHalDisplay::deinit()
{
    if (display)
    {
        lv_display_delete(display);
        display = nullptr;
    }
    
    if (fbBuffer != MAP_FAILED)
    {
        munmap(fbBuffer, fbSize);
        fbBuffer = nullptr;
    }
    
    if (fbFd != -1)
    {
        close(fbFd);
        fbFd = -1;
    }
    
    return HalResult::OK;
}

DisplayInfo LinuxHalDisplay::getDisplayInfo() const
{
    return displayInfo;
}

HalResult LinuxHalDisplay::setBacklight(uint8_t brightness)
{
    return HalResult::OK;
}

HalResult LinuxHalDisplay::backlightOn()
{
    return HalResult::OK;
}

HalResult LinuxHalDisplay::backlightOff()
{
    return HalResult::OK;
}

void LinuxHalDisplay::lock(uint32_t timeoutMs)
{
    if (timeoutMs == 0)
        displayMutex.lock();
    else
    {
        auto timeout = std::chrono::milliseconds(timeoutMs);
        displayMutex.try_lock_for(timeout);
    }
}

void LinuxHalDisplay::unlock()
{
    displayMutex.unlock();
}

lv_display_t* LinuxHalDisplay::getLvglDisplay()
{
    return display;
}
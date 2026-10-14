#pragma once

#include "lvgl.h"
#include <cstdint>
#include <string>

// LVGL on an in-memory display, so widgets build, lay out and render in the
// host tests. Time only moves through advance(), which runs the LVGL timers.
namespace headless
{

constexpr int kWidth = 1280;
constexpr int kHeight = 800;

void init();
// The display shows the top-left w x h of the buffer, so a test can stand in for the 800x480 or 720x720 panels
void resize(int w, int h);
int width();
int height();
lv_obj_t* screen();
void advance(uint32_t ms);
void render();

// Writes the given area of the last render as an 8-bit RGB PNG
bool writePng(const std::string& path, const lv_area_t& area);

} // namespace headless

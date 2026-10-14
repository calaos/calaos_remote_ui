#include "headless_lvgl.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace headless
{

namespace
{
uint32_t g_tick = 0;
lv_display_t* g_display = nullptr;
int g_width = kWidth;
int g_height = kHeight;
std::vector<uint8_t> g_storage;
uint8_t* g_frame = nullptr;
size_t g_frameSize = 0;

uint32_t tick() { return g_tick; }

void flush(lv_display_t* disp, const lv_area_t*, uint8_t*)
{
    lv_display_flush_ready(disp);
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0)
{
    static std::array<uint32_t, 256> table = []
    {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; n++)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();

    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        out.push_back(static_cast<uint8_t>(v >> s));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}
} // namespace

namespace
{
void createDisplay(int w, int h)
{
    g_width = w;
    g_height = h;
    g_display = lv_display_create(w, h);
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_XRGB8888);
    // LVGL asserts (and spins) on a buffer that is not aligned for its draw units
    g_frameSize = static_cast<size_t>(w) * h * 4;
    g_storage.assign(g_frameSize + 256, 0);
    g_frame = static_cast<uint8_t*>(lv_draw_buf_align(g_storage.data(), LV_COLOR_FORMAT_XRGB8888));
    lv_display_set_buffers(g_display, g_frame, nullptr, static_cast<uint32_t>(g_frameSize),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(g_display, flush);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
}
} // namespace

void init()
{
    if (g_display)
        return;

    lv_init();
    lv_tick_set_cb(tick);
    createDisplay(kWidth, kHeight);
}

void resize(int w, int h)
{
    init();
    if (w == g_width && h == g_height)
        return;
    lv_display_delete(g_display);
    createDisplay(w, h);
}

int width() { return g_width; }
int height() { return g_height; }

lv_obj_t* screen()
{
    return lv_screen_active();
}

void advance(uint32_t ms)
{
    // Small steps: a timer due inside the window must fire inside it
    for (uint32_t done = 0; done < ms; done += 10)
    {
        g_tick += 10;
        lv_timer_handler();
    }
}

void render()
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(g_display);
}

bool writePng(const std::string& path, const lv_area_t& area)
{
    const int x1 = std::max<int>(0, area.x1), y1 = std::max<int>(0, area.y1);
    const int x2 = std::min<int>(g_width - 1, area.x2), y2 = std::min<int>(g_height - 1, area.y2);
    const uint32_t w = x2 - x1 + 1, h = y2 - y1 + 1;

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (int y = y1; y <= y2; y++)
    {
        raw.push_back(0);
        for (int x = x1; x <= x2; x++)
        {
            const uint8_t* p = &g_frame[(static_cast<size_t>(y) * g_width + x) * 4];
            raw.push_back(p[2]);
            raw.push_back(p[1]);
            raw.push_back(p[0]);
        }
    }

    // zlib stream of stored blocks: no compressor needed
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw)
    {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < raw.size() || off == 0; off += 65535)
    {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF);
        z.push_back(n >> 8);
        z.push_back(~n & 0xFF);
        z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        if (raw.empty())
            break;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

} // namespace headless

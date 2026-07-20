#include "perf_bench.h"

#ifdef PERF_BENCH

#include "lvgl.h"
#include "logging.h"
#include "esp_timer.h"
#include <cstdio>

static const char* TAG = "PERF01";

// Scenes exercise the distinct cost centers of the landscape pipeline so we can
// tell bandwidth/rotate-bound from SW-render-bound:
//   FILL   : full-screen solid fill, PPA-eligible (radius 0, opa cover).
//            -> pure fill + PPA rotate + DSI scanout every frame. Bandwidth/rotate.
//   SWDRAW : grid of rounded panels recolored every frame. radius != 0 forces the
//            LVGL software complex renderer (PPA fill declines), full-screen dirty.
//            -> isolates single SW draw unit / XIP-from-PSRAM hot loop cost.
//   CLOCK  : one big Roboto-150 label, digits changing every frame.
//            -> the real screensaver workload (large glyph SW draw + rotate).
//   SWIPE  : a 2x-wide content container scrolled horizontally every frame.
//            -> the page-swipe workload (full-screen composite redraw + rotate).
enum { SCENE_FILL = 0, SCENE_SWDRAW, SCENE_CLOCK, SCENE_SWIPE, SCENE_COUNT };
static const char* kSceneNames[SCENE_COUNT] = { "FILL", "SWDRAW", "CLOCK", "SWIPE" };

static const uint32_t kSceneDurationMs = 6000;
static const uint32_t kMutatePeriodMs  = 10; // push as fast as the pipeline allows

static lv_obj_t*   s_root        = nullptr;
static lv_timer_t* s_mutateTimer = nullptr;
static lv_timer_t* s_sceneTimer  = nullptr;
static int         s_scene       = 0;
static uint32_t    s_frame       = 0;

// Scene-specific handles.
static lv_obj_t* s_label = nullptr;   // CLOCK
static lv_obj_t* s_swipe = nullptr;   // SWIPE (wide container)
#define SW_GRID_MAX 24
static lv_obj_t* s_grid[SW_GRID_MAX] = {nullptr};
static int       s_gridCount = 0;

static int32_t screenW() { return lv_obj_get_width(lv_screen_active()); }
static int32_t screenH() { return lv_obj_get_height(lv_screen_active()); }

static void styleBare(lv_obj_t* o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
}

static void buildScene(int idx)
{
    lv_obj_clean(s_root);
    s_label = nullptr;
    s_swipe = nullptr;
    s_gridCount = 0;
    for (int i = 0; i < SW_GRID_MAX; i++) s_grid[i] = nullptr;

    const int32_t W = screenW();
    const int32_t H = screenH();

    switch (idx) {
    case SCENE_FILL: {
        lv_obj_t* o = lv_obj_create(s_root);
        styleBare(o);
        lv_obj_set_size(o, W, H);
        lv_obj_set_pos(o, 0, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        s_grid[0] = o;
        s_gridCount = 1;
        break;
    }
    case SCENE_SWDRAW: {
        const int cols = 6, rows = 4; // 24 rounded panels tiling the screen
        const int32_t cw = W / cols, ch = H / rows;
        int n = 0;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols && n < SW_GRID_MAX; c++) {
                lv_obj_t* o = lv_obj_create(s_root);
                lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
                lv_obj_set_size(o, cw - 6, ch - 6);
                lv_obj_set_pos(o, c * cw + 3, r * ch + 3);
                lv_obj_set_style_radius(o, 24, 0);       // forces SW complex renderer
                lv_obj_set_style_border_width(o, 2, 0);
                lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
                s_grid[n++] = o;
            }
        }
        s_gridCount = n;
        break;
    }
    case SCENE_CLOCK: {
        s_label = lv_label_create(s_root);
        lv_obj_set_style_text_font(s_label, &roboto_bold_150, 0);
        lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
        lv_label_set_text(s_label, "00:00:00");
        lv_obj_center(s_label);
        break;
    }
    case SCENE_SWIPE: {
        // Wide container (2x screen) holding several colored panels + labels,
        // scrolled horizontally to emulate a page swipe.
        s_swipe = lv_obj_create(s_root);
        styleBare(s_swipe);
        lv_obj_set_size(s_swipe, W * 2, H);
        lv_obj_set_pos(s_swipe, 0, 0);
        lv_obj_set_style_bg_color(s_swipe, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(s_swipe, LV_OPA_COVER, 0);
        const int panels = 8;
        const int32_t pw = (W * 2) / panels;
        for (int i = 0; i < panels; i++) {
            lv_obj_t* p = lv_obj_create(s_swipe);
            lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_size(p, pw - 20, H - 80);
            lv_obj_set_pos(p, i * pw + 10, 40);
            lv_obj_set_style_radius(p, 16, 0);
            lv_obj_set_style_bg_color(p, lv_color_hsv_to_rgb((i * 45) % 360, 70, 90), 0);
            lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
            lv_obj_t* lbl = lv_label_create(p);
            lv_obj_set_style_text_font(lbl, &roboto_bold_60, 0);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
            lv_label_set_text_fmt(lbl, "Page %d", i + 1);
            lv_obj_center(lbl);
        }
        break;
    }
    default: break;
    }
}

static void mutateCb(lv_timer_t*)
{
    const uint32_t f = s_frame++;
    switch (s_scene) {
    case SCENE_FILL:
        if (s_grid[0])
            lv_obj_set_style_bg_color(s_grid[0], lv_color_hsv_to_rgb(f % 360, 100, 100), 0);
        break;
    case SCENE_SWDRAW:
        for (int i = 0; i < s_gridCount; i++)
            lv_obj_set_style_bg_color(s_grid[i], lv_color_hsv_to_rgb((f + i * 30) % 360, 80, 95), 0);
        break;
    case SCENE_CLOCK: {
        if (s_label) {
            char buf[16];
            uint32_t t = f; // fake ticking clock, changes every frame
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                     (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60), (unsigned)(t % 60));
            lv_label_set_text(s_label, buf);
            lv_obj_center(s_label);
        }
        break;
    }
    case SCENE_SWIPE: {
        if (s_swipe) {
            const int32_t W = screenW();
            const int32_t span = W;             // scroll range
            const int32_t period = span * 2;
            int32_t pos = (int32_t)((f * 8) % (uint32_t)period);
            int32_t x = pos < span ? -pos : -(period - pos); // triangle wave
            lv_obj_set_x(s_swipe, x);
        }
        break;
    }
    default: break;
    }
}

static void sceneCb(lv_timer_t*)
{
    s_scene = (s_scene + 1) % SCENE_COUNT;
    s_frame = 0;
    buildScene(s_scene);
    ESP_LOGI(TAG, "########## SCENE: %s (%d/%d) ##########",
             kSceneNames[s_scene], s_scene + 1, SCENE_COUNT);
}

// App-side FPS counter — display-stack agnostic (works with esp_lvgl_port OR
// esp_lvgl_adapter). Counts LVGL refresh cycles and logs FPS per ~2s window,
// attributed to the active scene. This is the metric the parent compares across
// pipeline changes now that the esp_lvgl_port flush instrumentation is unused.
static void fpsEventCb(lv_event_t*)
{
    static uint32_t frames = 0;
    static int64_t winStart = 0;
    frames++;
    int64_t now = esp_timer_get_time();
    if (winStart == 0) {
        winStart = now;
        return;
    }
    if (now - winStart >= 2000000) {
        double secs = (double)(now - winStart) / 1e6;
        ESP_LOGI(TAG, "FPS=%.1f (%lu frames / %.2fs) scene=%s",
                 frames / secs, (unsigned long)frames, secs, kSceneNames[s_scene]);
        frames = 0;
        winStart = now;
    }
}

void perfBenchStart()
{
    ESP_LOGI(TAG, "PERF_BENCH harness starting: %dx%d, scenes=%d, %ums each",
             (int)screenW(), (int)screenH(), SCENE_COUNT, (unsigned)kSceneDurationMs);

    lv_display_add_event_cb(lv_display_get_default(), fpsEventCb, LV_EVENT_REFR_READY, nullptr);

    s_root = lv_obj_create(lv_screen_active());
    styleBare(s_root);
    lv_obj_set_size(s_root, screenW(), screenH());
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    s_scene = 0;
    s_frame = 0;
    buildScene(s_scene);
    ESP_LOGI(TAG, "########## SCENE: %s (1/%d) ##########", kSceneNames[s_scene], SCENE_COUNT);

    s_mutateTimer = lv_timer_create(mutateCb, kMutatePeriodMs, nullptr);
    s_sceneTimer  = lv_timer_create(sceneCb, kSceneDurationMs, nullptr);
}

#endif // PERF_BENCH

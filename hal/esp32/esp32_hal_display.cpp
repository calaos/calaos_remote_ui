#include "esp32_hal_display.h"
#include "logging.h"
#include "board_config.h"
#include "bsp_board_extra.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lvgl_port.h"

#if BOARD_DISPLAY_ROTATION != 0
// PERF-01 custom rotating flush (increment 1: H1 + H8). See the big comment on
// init_custom_rotating_display() below and docs/backlog/PERF-01-ppa-rotation-fps.md.
#include <cstdint>
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "hal/color_types.h"
extern "C" {
// esp_lcd/priv_include (added to include path in main/CMakeLists.txt). This
// private header has no extern "C" guard of its own, so wrap it to keep C
// linkage for esp_async_fbcpy* from this C++ translation unit.
#include "esp_async_fbcpy.h"
}
#endif

static const char* TAG = "hal.display";

#if BOARD_DISPLAY_ROTATION != 0
// ─────────────────────────────────────────────────────────────────────────────
// Custom tear-free rotating display flush (ESP32-P4, portrait-native MIPI-DSI
// panel presented in landscape via a 90°/270° PPA rotation).
//
// Why a custom flush (PERF-01): esp_lvgl_port's PPA rotate wrote a SEPARATE PPA
// output buffer that draw_bitmap then copied again into the DPI framebuffer
// (double PSRAM traffic); esp_lvgl_adapter's TRIPLE_PARTIAL was fast but showed
// black on static UIs (framebuffers never converge). This flush:
//
//   H1  PPA-rotates each dirty LVGL stripe DIRECTLY into the BACK DPI
//       framebuffer (no intermediate buffer, no second copy) — .out.buffer is
//       the framebuffer, .out.block_offset_{x,y} is the rotated destination.
//   H8  Two DPI framebuffers + a vsync-latched page flip: we draw the whole
//       frame into the BACK fb, then esp_lcd_panel_draw_bitmap() with a pointer
//       INSIDE that fb triggers the DPI "no-copy" path (esp_lcd_panel_dpi.c),
//       which sets cur_fb_index so the DSI DMA switches scanout to it at the
//       next frame boundary (tear-free). We then wait one frame boundary before
//       reusing the retired fb as the next back buffer.
//
// STATIC correctness: LVGL PARTIAL mode only flushes the dirty sub-rects of a
// frame, so before applying the first dirty stripe of a new frame we replay the
// currently-scanned (front) fb into the back fb with esp_async_fbcpy (2D-DMA),
// so un-redrawn regions carry over. Without this, static screens would show
// stale/black content in the alternate framebuffer.
//
// Rotation math is copied from the HW-verified esp_lvgl_adapter bridge
// (rotate_copy_strided_region): BOARD_DISPLAY_ROTATION==90 maps to PPA angle 90
// with dest offset (x=area.y1, y=panelH-1-area.x2); ==270 maps to PPA angle 270
// with (x=panelW-1-area.y2, y=area.x1). area is in LVGL LOGICAL landscape coords
// (LVGL renders logically because matrix_rotation is OFF; it still rotates touch
// points itself because the display rotation is set — so touch needs no fixup).
//
// Cache coherency: the CPU never reads or writes the framebuffers directly. All
// framebuffer writes are DMA (PPA output window + async_fbcpy), which manage
// their own cache; scanout reads PSRAM directly. draw_bitmap's whole-fb C2M
// writeback finds no CPU-dirty framebuffer lines, so it preserves the DMA data.
//
// Increment 1 uses BLOCKING PPA and a per-frame full-fb replay; H2 (internal-
// SRAM draw buffer) and H3 (non-blocking PPA pipeline + overlapped replay) come
// next.
// ─────────────────────────────────────────────────────────────────────────────

// PPA rotation angle for this board's landscape rotation.
#if BOARD_DISPLAY_ROTATION == 90
    #define HAL_PPA_ROTATION PPA_SRM_ROTATION_ANGLE_90
#elif BOARD_DISPLAY_ROTATION == 180
    #define HAL_PPA_ROTATION PPA_SRM_ROTATION_ANGLE_180
#else // 270
    #define HAL_PPA_ROTATION PPA_SRM_ROTATION_ANGLE_270
#endif

// Height (in LVGL logical lines) of each partial draw buffer. Larger buffers
// amortize the fixed per-PPA-op overhead. Increment 2 (H2) prefers two of these
// in INTERNAL SRAM (fast, no PSRAM read contention with scanout); SRAM is scarce
// so the SRAM size is smaller, with a PSRAM fallback. logical width is
// BOARD_DISPLAY_WIDTH.
#define HAL_DRAW_BUF_LINES_SRAM   64   // internal-SRAM attempt (1280*64*2 = 160 KB each)
#define HAL_DRAW_BUF_LINES_SRAM2  48   // smaller SRAM attempt (1280*48*2 = 120 KB each)
#define HAL_DRAW_BUF_LINES_PSRAM 120   // PSRAM fallback

// Force a full (whole-screen) replay for the first few frames so BOTH
// framebuffers converge to complete frames regardless of the dirty pattern.
#define HAL_WARMUP_FRAMES 3

struct CustomFlushCtx {
    esp_lcd_panel_handle_t     panel = nullptr;
    ppa_client_handle_t        ppa = nullptr;
    esp_async_fbcpy_handle_t   fbcpy = nullptr;
    SemaphoreHandle_t          vsync_sem = nullptr;  // given by on_frame_buf_complete (ISR)
    SemaphoreHandle_t          fbcpy_sem = nullptr;  // given by async_fbcpy done cb (ISR)
    SemaphoreHandle_t          ppa_sem = nullptr;    // given by PPA on_trans_done (ISR)
    uint8_t*                   fb[2] = {nullptr, nullptr};
    size_t                     fb_size = 0;
    uint16_t                   panel_w = 0;          // physical fb width  (out.pic_w)
    uint16_t                   panel_h = 0;          // physical fb height (out.pic_h)
    int                        draw_index = 1;       // fb LVGL renders into
    int                        scan_index = 0;       // fb currently scanned out
    bool                       frame_active = false; // false => next flush starts a new frame
    bool                       ppa_pending = false;  // an enqueued rotate awaits its done sem
    int                        warmup = HAL_WARMUP_FRAMES;
    // Dirty union (in PHYSICAL/rotated fb coords) of the CURRENT frame's rotates,
    // and of the PREVIOUS frame's (what front and back differ by => the replay).
    int32_t                    cur_x1 = 0, cur_y1 = 0, cur_x2 = -1, cur_y2 = -1;
    int32_t                    prev_x1 = 0, prev_y1 = 0, prev_x2 = -1, prev_y2 = -1;
    bool                       prev_valid = false;
};

// Frame-boundary callback: fires once per DSI frame DMA completion. Must live in
// IRAM (the DPI driver enforces esp_ptr_in_iram) and touch no flash.
static IRAM_ATTR bool hal_on_frame_buf_complete(esp_lcd_panel_handle_t /*panel*/,
                                                esp_lcd_dpi_panel_event_data_t* /*edata*/,
                                                void* user_ctx)
{
    CustomFlushCtx* ctx = (CustomFlushCtx*)user_ctx;
    BaseType_t need_yield = pdFALSE;
    if (ctx->vsync_sem) {
        xSemaphoreGiveFromISR(ctx->vsync_sem, &need_yield);
    }
    return need_yield == pdTRUE;
}

// async_fbcpy completion callback (runs in DMA2D ISR context).
static IRAM_ATTR bool hal_on_fbcpy_done(esp_async_fbcpy_handle_t /*mcp*/,
                                        esp_async_fbcpy_event_data_t* /*edata*/,
                                        void* cb_args)
{
    CustomFlushCtx* ctx = (CustomFlushCtx*)cb_args;
    BaseType_t need_yield = pdFALSE;
    if (ctx->fbcpy_sem) {
        xSemaphoreGiveFromISR(ctx->fbcpy_sem, &need_yield);
    }
    return need_yield == pdTRUE;
}

// PPA rotate completion callback (runs in PPA ISR context, see ppa_core.c).
static IRAM_ATTR bool hal_on_ppa_done(ppa_client_handle_t /*client*/,
                                      ppa_event_data_t* /*edata*/,
                                      void* user_data)
{
    CustomFlushCtx* ctx = (CustomFlushCtx*)user_data;
    BaseType_t need_yield = pdFALSE;
    if (ctx->ppa_sem) {
        xSemaphoreGiveFromISR(ctx->ppa_sem, &need_yield);
    }
    return need_yield == pdTRUE;
}

// Submit a front->back framebuffer copy of a physical sub-rect and block until
// it finishes (2D-DMA). rect is clamped to the panel; x is aligned out to 16 px
// to stay clear of any DMA2D horizontal-burst alignment constraints (over-copy
// is harmless — same-layout buffers).
static void hal_replay_region(CustomFlushCtx* ctx, int src_idx, int dst_idx,
                              int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (x2 < x1 || y2 < y1) return;
    x1 &= ~0xF;                         // align down to 16 px
    x2 |= 0xF;                          // align up   to 16 px
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= ctx->panel_w) x2 = ctx->panel_w - 1;
    if (y2 >= ctx->panel_h) y2 = ctx->panel_h - 1;

    esp_async_fbcpy_trans_desc_t tr = {};
    tr.src_buffer = ctx->fb[src_idx];
    tr.dst_buffer = ctx->fb[dst_idx];
    tr.src_buffer_size_x = ctx->panel_w;   // full stride
    tr.src_buffer_size_y = ctx->panel_h;
    tr.dst_buffer_size_x = ctx->panel_w;
    tr.dst_buffer_size_y = ctx->panel_h;
    tr.src_offset_x = x1;
    tr.src_offset_y = y1;
    tr.dst_offset_x = x1;
    tr.dst_offset_y = y1;
    tr.copy_size_x = x2 - x1 + 1;
    tr.copy_size_y = y2 - y1 + 1;
    tr.pixel_format_unique_id.color_type_id = COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565);
    if (esp_async_fbcpy(ctx->fbcpy, &tr, hal_on_fbcpy_done, ctx) == ESP_OK) {
        xSemaphoreTake(ctx->fbcpy_sem, portMAX_DELAY);
    }
}

static void hal_custom_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    CustomFlushCtx* ctx = (CustomFlushCtx*)lv_display_get_user_data(disp);

    // ── Start of a new frame: reconstruct the back fb so regions LVGL will NOT
    //    redraw this frame carry over (static content), keeping both buffers
    //    convergent for tear-free double buffering.
    //
    //    Invariant maintained: after frame K, fb[scan]=frame K, fb[draw]=frame
    //    K-1. So at frame K's start fb[draw] holds frame K-2 and fb[scan]=frame
    //    K-1; they differ EXACTLY by frame K-1's dirty union. Replaying only that
    //    region front->back makes fb[draw]=frame K-1 (the base) at minimal cost.
    //    Full-screen frames overwrite the whole back fb anyway, so when the
    //    previous dirty union covers ~the whole panel we SKIP the replay.
    if (!ctx->frame_active) {
        if (ctx->warmup > 0 || !ctx->prev_valid) {
            // Force whole-screen convergence early / when we have no history.
            hal_replay_region(ctx, ctx->scan_index, ctx->draw_index,
                              0, 0, ctx->panel_w - 1, ctx->panel_h - 1);
        } else {
            // Always replay the previous frame's dirty union front->back so the
            // back fb catches up to the last displayed frame before this frame's
            // dirty is applied. For a full-screen previous frame this is a full
            // replay — that is REQUIRED for correctness: a former ">=90% => skip"
            // optimization caused a 1 Hz flicker where, after a full-screen change
            // (e.g. the screensaver turning on), the next partial frame (clock
            // tick) skipped the replay and the alternate framebuffer still showed
            // the stale Calaos page. Correctness wins over the full-screen cost.
            hal_replay_region(ctx, ctx->scan_index, ctx->draw_index,
                              ctx->prev_x1, ctx->prev_y1, ctx->prev_x2, ctx->prev_y2);
        }
        // Begin accumulating this frame's dirty union.
        ctx->cur_x1 = ctx->cur_y1 = INT32_MAX;
        ctx->cur_x2 = ctx->cur_y2 = INT32_MIN;
        ctx->frame_active = true;
    }

    // ── H1: PPA-rotate this dirty stripe directly into the back framebuffer.
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;

    uint32_t out_off_x, out_off_y;   // physical top-left of the rotated block
    uint32_t blk_w, blk_h;           // physical block dims (for dirty tracking)
#if BOARD_DISPLAY_ROTATION == 90
    out_off_x = (uint32_t)area->y1;
    out_off_y = (uint32_t)(ctx->panel_h - 1 - area->x2);
    blk_w = h; blk_h = w;
#elif BOARD_DISPLAY_ROTATION == 180
    out_off_x = (uint32_t)(ctx->panel_w - 1 - area->x2);
    out_off_y = (uint32_t)(ctx->panel_h - 1 - area->y2);
    blk_w = w; blk_h = h;
#else // 270
    out_off_x = (uint32_t)(ctx->panel_w - 1 - area->y2);
    out_off_y = (uint32_t)area->x1;
    blk_w = h; blk_h = w;
#endif

    // Accumulate the physical dirty union (used by the next frame's replay).
    if ((int32_t)out_off_x < ctx->cur_x1) ctx->cur_x1 = out_off_x;
    if ((int32_t)out_off_y < ctx->cur_y1) ctx->cur_y1 = out_off_y;
    if ((int32_t)(out_off_x + blk_w - 1) > ctx->cur_x2) ctx->cur_x2 = out_off_x + blk_w - 1;
    if ((int32_t)(out_off_y + blk_h - 1) > ctx->cur_y2) ctx->cur_y2 = out_off_y + blk_h - 1;

    ppa_srm_oper_config_t oper = {};
    oper.in.buffer          = px_map;
    oper.in.pic_w           = w;   // compact partial stripe: stride == width
    oper.in.pic_h           = h;
    oper.in.block_w         = w;
    oper.in.block_h         = h;
    oper.in.block_offset_x  = 0;
    oper.in.block_offset_y  = 0;
    oper.in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
    oper.out.buffer         = ctx->fb[ctx->draw_index];
    oper.out.buffer_size    = ctx->fb_size;
    oper.out.pic_w          = ctx->panel_w;
    oper.out.pic_h          = ctx->panel_h;
    oper.out.block_offset_x = out_off_x;
    oper.out.block_offset_y = out_off_y;
    oper.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
    oper.rotation_angle     = HAL_PPA_ROTATION;
    oper.scale_x            = 1.0f;
    oper.scale_y            = 1.0f;
    oper.rgb_swap           = false;
    oper.byte_swap          = false;
    oper.mode               = PPA_TRANS_MODE_NON_BLOCKING;
    oper.user_data          = ctx;  // passed to hal_on_ppa_done (on_trans_done); NULL here faults

    // ── H3: pipeline the rotate. Before overwriting the draw buffer that LVGL
    //    will reuse next, wait for the PREVIOUS rotate to finish (keeps at most
    //    one PPA op in flight; safe with LVGL's 2 draw buffers). Then enqueue
    //    this rotate non-blocking and let LVGL render the next stripe while the
    //    PPA rotates this one.
    if (ctx->ppa_pending) {
        xSemaphoreTake(ctx->ppa_sem, portMAX_DELAY);
        ctx->ppa_pending = false;
    }
    ppa_do_scale_rotate_mirror(ctx->ppa, &oper);
    ctx->ppa_pending = true;

    // ── H8: on the last stripe of the frame, ensure the whole back fb is
    //    rotated, flip scanout to it, and wait for the flip to latch before
    //    reusing the retired fb.
    if (lv_display_flush_is_last(disp)) {
        // Drain the final rotate so the back fb is fully rendered before scanout.
        if (ctx->ppa_pending) {
            xSemaphoreTake(ctx->ppa_sem, portMAX_DELAY);
            ctx->ppa_pending = false;
        }
        // Pointer is inside a DPI framebuffer => no-copy path: sets cur_fb_index
        // to draw_index (latched on the next frame boundary) + msync writeback.
        esp_lcd_panel_draw_bitmap(ctx->panel, 0, 0, ctx->panel_w, ctx->panel_h,
                                  ctx->fb[ctx->draw_index]);
        // Drain any stale give, then wait for the next frame boundary => the DSI
        // DMA has re-armed with the new fb and the old one is free to reuse.
        xSemaphoreTake(ctx->vsync_sem, 0);
        xSemaphoreTake(ctx->vsync_sem, portMAX_DELAY);
        ctx->scan_index = ctx->draw_index;
        ctx->draw_index = 1 - ctx->draw_index;

        // Promote this frame's dirty union to "previous" for the next replay.
        if (ctx->cur_x2 >= ctx->cur_x1 && ctx->cur_y2 >= ctx->cur_y1) {
            ctx->prev_x1 = ctx->cur_x1; ctx->prev_y1 = ctx->cur_y1;
            ctx->prev_x2 = ctx->cur_x2; ctx->prev_y2 = ctx->cur_y2;
            ctx->prev_valid = true;
        } else {
            ctx->prev_valid = false;
        }
        if (ctx->warmup > 0) ctx->warmup--;
        ctx->frame_active = false;
    }

    lv_display_flush_ready(disp);
}

// Builds the LVGL display + custom flush pipeline. Returns the display or null.
static lv_display_t* init_custom_rotating_display(const bsp_lcd_handles_t& lcd,
                                                  int panelHRes, int panelVRes)
{
    CustomFlushCtx* ctx = new CustomFlushCtx();
    ctx->panel   = lcd.panel;
    ctx->panel_w = (uint16_t)panelHRes;   // physical (portrait-native) fb width
    ctx->panel_h = (uint16_t)panelVRes;   // physical fb height
    ctx->fb_size = (size_t)panelHRes * panelVRes * 2;  // RGB565

    // Grab the 2 DPI framebuffers (PSRAM, 128B aligned, from the BSP panel).
    if (esp_lcd_dpi_panel_get_frame_buffer(ctx->panel, 2, (void**)&ctx->fb[0], (void**)&ctx->fb[1]) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get 2 DPI framebuffers (need CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2)");
        delete ctx;
        return nullptr;
    }
    // Start both framebuffers black so the very first frame (scanned before the
    // first flip) shows no garbage.
    for (int i = 0; i < 2; i++) {
        memset(ctx->fb[i], 0, ctx->fb_size);
        esp_cache_msync(ctx->fb[i], ctx->fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }

    ctx->vsync_sem = xSemaphoreCreateCounting(1, 0);
    ctx->fbcpy_sem = xSemaphoreCreateBinary();
    ctx->ppa_sem   = xSemaphoreCreateCounting(2, 0);  // H3: PPA rotate completions
    if (!ctx->vsync_sem || !ctx->fbcpy_sem || !ctx->ppa_sem) {
        ESP_LOGE(TAG, "Failed to create flush semaphores");
        delete ctx;
        return nullptr;
    }

    // Dedicated PPA SRM client for the rotation (separate from the vendored LVGL
    // LV_USE_PPA fill/img draw unit's own client — they don't share state).
    // H3: NON_BLOCKING pipeline — allow >=2 pending transactions and get a
    // completion callback so the flush can enqueue a rotate and let LVGL render
    // the next stripe in parallel.
    ppa_client_config_t ppaCfg = {};
    ppaCfg.oper_type = PPA_OPERATION_SRM;
    ppaCfg.max_pending_trans_num = 2;
    if (ppa_register_client(&ppaCfg, &ctx->ppa) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA SRM client");
        delete ctx;
        return nullptr;
    }
    ppa_event_callbacks_t ppaCbs = {};
    ppaCbs.on_trans_done = hal_on_ppa_done;
    if (ppa_client_register_event_callbacks(ctx->ppa, &ppaCbs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA event callbacks");
        ppa_unregister_client(ctx->ppa);
        delete ctx;
        return nullptr;
    }

    // 2D-DMA engine for the per-frame front->back replay copy.
    esp_async_fbcpy_config_t fbcpyCfg = {};
    if (esp_async_fbcpy_install(&fbcpyCfg, &ctx->fbcpy) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install esp_async_fbcpy");
        ppa_unregister_client(ctx->ppa);
        delete ctx;
        return nullptr;
    }

    // Frame-boundary callback for the vsync-latched flip wait.
    // IDF 5.5.5 renamed on_refresh_done to on_frame_buf_complete (same union
    // slot, same signature); 5.5.4 and older only have the old name.
    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 5)
    cbs.on_frame_buf_complete = hal_on_frame_buf_complete;
#else
    cbs.on_refresh_done = hal_on_frame_buf_complete;
#endif
    if (esp_lcd_dpi_panel_register_event_callbacks(ctx->panel, &cbs, ctx) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DPI event callbacks");
        esp_async_fbcpy_uninstall(ctx->fbcpy);
        ppa_unregister_client(ctx->ppa);
        delete ctx;
        return nullptr;
    }

    // Create the LVGL display at panel-native resolution and set the landscape
    // rotation. matrix_rotation stays OFF, so LVGL renders in LOGICAL landscape
    // coords (flush areas are landscape) but still rotates touch input for us.
    lv_display_t* disp = lv_display_create(panelHRes, panelVRes);
    if (!disp) {
        ESP_LOGE(TAG, "lv_display_create failed");
        delete ctx;
        return nullptr;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, ctx);
    lv_display_set_flush_cb(disp, hal_custom_flush_cb);

    // ── H2: prefer two partial draw buffers in INTERNAL SRAM (SW render writes
    //    SRAM ~350 MB/s and the PPA reads its input from SRAM, so only the rotate
    //    WRITE hits PSRAM — removing PSRAM read contention with the DSI scanout).
    //    SRAM is scarce, so try progressively smaller heights, then fall back to
    //    PSRAM. All buffers are 128B aligned (PPA/L2 cache line). The PPA SRM
    //    accepts an internal-SRAM in.buffer with a PSRAM out.buffer.
    const int logicalW = BOARD_DISPLAY_WIDTH;   // 1280 (landscape horizontal res)
    void* buf1 = nullptr;
    void* buf2 = nullptr;
    size_t drawBufBytes = 0;

    struct { uint32_t caps; int lines; const char* where; } attempts[] = {
        { MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, HAL_DRAW_BUF_LINES_SRAM,  "internal-SRAM" },
        { MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, HAL_DRAW_BUF_LINES_SRAM2, "internal-SRAM" },
        { MALLOC_CAP_SPIRAM,                    HAL_DRAW_BUF_LINES_PSRAM, "PSRAM" },
    };
    for (auto& a : attempts) {
        const size_t bytes = (size_t)logicalW * a.lines * 2;
        // Need TWO contiguous blocks: gate on the largest free block for internal
        // SRAM so we don't fragment or fail mid-way.
        if ((a.caps & MALLOC_CAP_INTERNAL) &&
            heap_caps_get_largest_free_block(a.caps) < bytes) {
            continue;
        }
        buf1 = heap_caps_aligned_alloc(128, bytes, a.caps);
        buf2 = heap_caps_aligned_alloc(128, bytes, a.caps);
        if (buf1 && buf2) {
            drawBufBytes = bytes;
            ESP_LOGI(TAG, "LVGL draw buffers: 2 x %ux%d in %s (%u KB each)",
                     (unsigned)logicalW, a.lines, a.where, (unsigned)(bytes / 1024));
            break;
        }
        if (buf1) { heap_caps_free(buf1); buf1 = nullptr; }
        if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
        if (a.caps & MALLOC_CAP_INTERNAL) {
            ESP_LOGW(TAG, "Internal SRAM draw buffers (%u KB each) did not fit, trying smaller/PSRAM",
                     (unsigned)(bytes / 1024));
        }
    }
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        delete ctx;
        return nullptr;
    }
    lv_display_set_buffers(disp, buf1, buf2, drawBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

#if BOARD_DISPLAY_ROTATION == 90
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#elif BOARD_DISPLAY_ROTATION == 180
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
#else
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
#endif

    return disp;
}
#endif // BOARD_DISPLAY_ROTATION != 0

HalResult Esp32HalDisplay::init()
{
    // Panel native (pre-rotation) dimensions, derived from BOARD_DISPLAY_* + rotation.
    // We don't read BSP_LCD_H_RES/V_RES: two BSPs export bsp/display.h and the 4b
    // (managed) one always defines 720x720, which wins the include search even when
    // EXCLUDE_COMPONENTS removes its sources.
    #if BOARD_DISPLAY_ROTATION == 90 || BOARD_DISPLAY_ROTATION == 270
        constexpr int kPanelHRes = BOARD_DISPLAY_HEIGHT;
        constexpr int kPanelVRes = BOARD_DISPLAY_WIDTH;
    #else
        constexpr int kPanelHRes = BOARD_DISPLAY_WIDTH;
        constexpr int kPanelVRes = BOARD_DISPLAY_HEIGHT;
    #endif

    // Initialize the esp_lvgl_port task + tick + mutex. We keep the port for all
    // the non-display plumbing (LVGL task, lvgl_port_lock/unlock, touch) on every
    // board; only the display registration differs by rotation.
    lvgl_port_cfg_t portCfg = ESP_LVGL_PORT_INIT_CONFIG();
    // Default LVGL task stack (7168 B) overflows on LVGL 9.5 + PPA when building
    // deep widget trees (CalaosPage + tabs + AboutPage). Bump to 16 KB.
    portCfg.task_stack = 16384;
    if (lvgl_port_init(&portCfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init LVGL port");
        return HalResult::ERROR;
    }

    if (bsp_display_brightness_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init display brightness");
        return HalResult::ERROR;
    }

    // Create the MIPI-DSI panel + IO from the BSP (builds the esp_lcd panel only;
    // it does not bring up LVGL or register vsync/flush callbacks).
    bsp_lcd_handles_t lcdHandles = {};
    if (bsp_display_new_with_handles(nullptr, &lcdHandles) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create display panel");
        return HalResult::ERROR;
    }
    panelHandle = lcdHandles.panel;

#if BOARD_DISPLAY_ROTATION == 0
    // ── Non-rotated boards (e.g. 86-panel): plain esp_lvgl_port partial DSI. ──
    const lvgl_port_display_cfg_t dispCfg = {
        .io_handle = lcdHandles.io,
        .panel_handle = lcdHandles.panel,
        .control_handle = lcdHandles.control,
        .buffer_size = kPanelHRes * 200,
        .double_buffer = true,
        .trans_size = 0,
        .hres = kPanelHRes,
        .vres = kPanelVRes,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
            .full_refresh = 0,
            .direct_mode = 0,
        }
    };

    const lvgl_port_display_dsi_cfg_t dsiCfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };

    display = lvgl_port_add_disp_dsi(&dispCfg, &dsiCfg);
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return HalResult::ERROR;
    }
#else
    // ── Rotated boards (7"/8"/10"): custom tear-free rotating flush (H1 + H8). ──
    if (lvgl_port_lock(0))
    {
        display = init_custom_rotating_display(lcdHandles, kPanelHRes, kPanelVRes);
        lvgl_port_unlock();
    }
    if (!display)
    {
        ESP_LOGE(TAG, "Failed to init custom rotating display");
        return HalResult::ERROR;
    }
#endif

    lv_display_set_dpi(display, 180);

    displayInfo.width = BOARD_DISPLAY_WIDTH;
    displayInfo.height = BOARD_DISPLAY_HEIGHT;
    displayInfo.colorDepth = BOARD_DISPLAY_COLOR_DEPTH;

    ESP_LOGI(TAG, "Display initialized: %dx%d, %d-bit (rot=%d, lv hres=%d vres=%d, scr=%dx%d)",
             displayInfo.width, displayInfo.height, displayInfo.colorDepth,
             (int)BOARD_DISPLAY_ROTATION,
             (int)lv_display_get_horizontal_resolution(display),
             (int)lv_display_get_vertical_resolution(display),
             (int)lv_obj_get_width(lv_screen_active()),
             (int)lv_obj_get_height(lv_screen_active()));

    return HalResult::OK;
}

HalResult Esp32HalDisplay::deinit()
{
    display = nullptr;
    return HalResult::OK;
}

DisplayInfo Esp32HalDisplay::getDisplayInfo() const
{
    return displayInfo;
}

HalResult Esp32HalDisplay::setBacklight(uint8_t brightness)
{
    esp_err_t ret = bsp_display_brightness_set(brightness);
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalDisplay::backlightOn()
{
    esp_err_t ret = bsp_display_backlight_on();
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalDisplay::backlightOff()
{
    esp_err_t ret = bsp_display_backlight_off();
    return (ret == ESP_OK) ? HalResult::OK : HalResult::ERROR;
}

HalResult Esp32HalDisplay::displayOff()
{
    if (!panelHandle)
        return HalResult::ERROR;

    esp_err_t ret = esp_lcd_panel_disp_on_off(panelHandle, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to turn off display panel: %s", esp_err_to_name(ret));
        return HalResult::ERROR;
    }
    return HalResult::OK;
}

void Esp32HalDisplay::lock(uint32_t timeoutMs)
{
    lvgl_port_lock(timeoutMs);
}

bool Esp32HalDisplay::tryLock(uint32_t timeoutMs)
{
    // Go straight to esp_lvgl_port rather than through bsp_display_lock(): the two
    // BSPs disagree on the return convention. esp32_p4_wifi6_touch_lcd_x returns
    // esp_err_t (ESP_OK == 0 on success), esp32_p4_wifi6_touch_lcd_4b returns bool
    // (true == 1 on success). Comparing against ESP_OK inverted the result on the
    // 4b/86-panel: callers took the mutex, were told they had failed, and returned
    // without unlocking, deadlocking the LVGL task for good. lvgl_port_lock() is
    // what both BSPs delegate to and is unambiguously bool.
    return lvgl_port_lock(timeoutMs);
}

void Esp32HalDisplay::unlock()
{
    lvgl_port_unlock();
}

lv_display_t* Esp32HalDisplay::getLvglDisplay()
{
    return display;
}

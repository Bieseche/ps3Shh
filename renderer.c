#include "renderer.h"
#include "font8x8.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

/* ── CONFIGURAÇÕES DO FRAMEBUFFER ── */
#define RSX_FB_COUNT  2
#define PITCH         (SCREEN_W * 4)
#define FB_SIZE       (PITCH * SCREEN_H)

static uint32_t *fb_ptr   [RSX_FB_COUNT];
static uint32_t  fb_offset[RSX_FB_COUNT];
static int       cur_fb   = 0;

typedef enum { FONT_SMALL = 0, FONT_MEDIUM, FONT_LARGE } FontSize;
static const int FONT_SCALE[] = { 1, 2, 3 };

/* ── FUNÇÕES DE AUXÍLIO (COR E BLEND) ── */
static inline uint32_t rgba_to_argb(uint32_t rgba) {
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    uint8_t a = (rgba      ) & 0xFF;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}

static inline uint32_t blend_pixel(uint32_t dst, uint32_t src_argb) {
    uint8_t sa = (src_argb >> 24) & 0xFF;
    if (sa == 0xFF) return src_argb;
    if (sa == 0x00) return dst;
    uint8_t sr = (src_argb >> 16) & 0xFF;
    uint8_t sg = (src_argb >>  8) & 0xFF;
    uint8_t sb = (src_argb      ) & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >>  8) & 0xFF;
    uint8_t db = (dst      ) & 0xFF;
    uint8_t r  = (uint8_t)((sr * sa + dr * (255 - sa)) / 255);
    uint8_t g  = (uint8_t)((sg * sa + dg * (255 - sa)) / 255);
    uint8_t b  = (uint8_t)((sb * sa + db * (255 - sa)) / 255);
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline uint32_t brighten(uint32_t c, uint8_t amt) {
    uint8_t r = (c >> 24) & 0xFF; uint8_t g = (c >> 16) & 0xFF;
    uint8_t b = (c >>  8) & 0xFF; uint8_t a = (c      ) & 0xFF;
    r = (r + amt > 255) ? 255 : r + amt;
    g = (g + amt > 255) ? 255 : g + amt;
    b = (b + amt > 255) ? 255 : b + amt;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b <<  8) | a;
}

static inline uint32_t status_color(BlockStatus s) {
    switch (s) {
        case BLOCK_GOOD:    return COL_GOOD;
        case BLOCK_WARNING: return COL_WARN;
        case BLOCK_BAD:     return COL_BAD;
        default:            return COL_UNSCANNED;
    }
}

static inline int blink(uint32_t fc) { return (fc / 15) & 1; }

/* ── RASTERIZER ── */
static void draw_rect(gcmContextData *ctx, int x, int y, int w, int h, uint32_t rgba) {
    (void)ctx;
    if (w <= 0 || h <= 0) return;
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + w > SCREEN_W) ? SCREEN_W : x + w;
    int y2 = (y + h > SCREEN_H) ? SCREEN_H : y + h;
    if (x1 >= x2 || y1 >= y2) return;
    uint32_t argb = rgba_to_argb(rgba);
    uint32_t *p   = fb_ptr[cur_fb];
    uint8_t   a   = rgba & 0xFF;
    if (a == 0xFF) {
        for (int row = y1; row < y2; row++) {
            uint32_t *line = p + row * SCREEN_W + x1;
            for (int col = 0, len = x2 - x1; col < len; col++) line[col] = argb;
        }
    } else {
        for (int row = y1; row < y2; row++) {
            uint32_t *line = p + row * SCREEN_W + x1;
            for (int col = 0, len = x2 - x1; col < len; col++) line[col] = blend_pixel(line[col], argb);
        }
    }
}

static void draw_rect_outline(gcmContextData *ctx, int x, int y, int w, int h, uint32_t rgba, int t) {
    draw_rect(ctx, x, y, w, t, rgba);
    draw_rect(ctx, x, y + h - t, w, t, rgba);
    draw_rect(ctx, x, y, t, h, rgba);
    draw_rect(ctx, x + w - t, y, t, h, rgba);
}

/* ── TEXT RENDER ── */
static void render_char(int x, int y, char ch, uint32_t rgba, int scale) {
    uint8_t code = (uint8_t)ch;
    if (code < FONT8X8_FIRST || code > FONT8X8_LAST) return;
    const uint8_t *glyph = font8x8_data[code - FONT8X8_FIRST];
    uint32_t argb = rgba_to_argb(rgba);
    uint32_t *p   = fb_ptr[cur_fb];
    for (int gy = 0; gy < 8; gy++) {
        uint8_t bits = glyph[gy];
        for (int gx = 0; gx < 8; gx++) {
            if (!(bits & (0x80 >> gx))) continue;
            for (int sy = 0; sy < scale; sy++) {
                int py = y + gy * scale + sy;
                if (py < 0 || py >= SCREEN_H) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + gx * scale + sx;
                    if (px < 0 || px >= SCREEN_W) continue;
                    p[py * SCREEN_W + px] = argb;
                }
            }
        }
    }
}

static void render_text(gcmContextData *ctx, int x, int y, const char *text, uint32_t rgba, FontSize size) {
    (void)ctx; if (!text) return;
    int scale = FONT_SCALE[size];
    int char_w = 8 * scale + scale;
    int cx = x;
    for (const char *c = text; *c; c++, cx += char_w) render_char(cx, y, *c, rgba, scale);
}

/* ── UI COMPONENTS ── */
static void render_header(gcmContextData *ctx, const ScanState *s, uint32_t fc) {
    draw_rect(ctx, 0, 0, SCREEN_W, 72, COL_PANEL);
    draw_rect(ctx, 0, 70, SCREEN_W, 2, COL_ACCENT);
    render_text(ctx, 40, 14, "PS3 SafeHDD Health", COL_TEXT, FONT_LARGE);
    const char *label = s->scan_active ? (blink(fc)?"[ SCANNING ]":"[          ]") : (s->scan_done?"[ DONE ]":"[ READY ]");
    render_text(ctx, SCREEN_W - 230, 26, label, s->scan_done?COL_GOOD:COL_ACCENT2, FONT_MEDIUM);
}

static void render_grid(gcmContextData *ctx, const BlockGrid *g, const ScanState *s, uint32_t fc) {
    int pw = GRID_COLS * (CELL_W + CELL_GAP) + 20;
    int ph = GRID_ROWS * (CELL_H + CELL_GAP) + 34;
    draw_rect(ctx, GRID_ORIGIN_X-12, GRID_ORIGIN_Y-26, pw, ph, COL_PANEL);
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int idx = r * GRID_COLS + c;
            int cx = GRID_ORIGIN_X + c * (CELL_W + CELL_GAP);
            int cy = GRID_ORIGIN_Y + r * (CELL_H + CELL_GAP);
            uint32_t color = status_color(g->cells[r][c]);
            if (s->scan_active && idx == (int)s->current_index && blink(fc)) color = 0xFFFFFFFF;
            draw_rect(ctx, cx, cy, CELL_W, CELL_H, color);
        }
    }
}

static void render_stats(gcmContextData *ctx, const ScanState *s) {
    int sx = STATS_X, sy = STATS_Y;
    draw_rect(ctx, sx-12, sy-12, 370, 300, COL_PANEL);
    render_text(ctx, sx, sy-8, "SCAN STATISTICS", COL_ACCENT, FONT_SMALL);
    char buf[32];
    const char *labels[] = {"TOTAL:", "GOOD:", "WARN:", "BAD:"};
    uint32_t vals[] = {s->total, s->good_count, s->warn_count, s->bad_count};
    for(int i=0; i<4; i++) {
        snprintf(buf, sizeof(buf), "%s %u", labels[i], vals[i]);
        render_text(ctx, sx, sy+30+(i*25), buf, COL_TEXT, FONT_SMALL);
    }
}

/* ── RENDERER CORE ── */
void renderer_init(void) {
    videoConfiguration vcfg;
    memset(&vcfg, 0, sizeof(vcfg));
    vcfg.resolution = VIDEO_RESOLUTION_720;
    vcfg.format = VIDEO_BUFFER_FORMAT_XRGB;
    vcfg.pitch = PITCH;
    vcfg.aspect = VIDEO_ASPECT_16_9;
    videoConfigure(0, &vcfg, NULL, 0);

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        fb_ptr[i] = (uint32_t *)rsxMemalign(64, FB_SIZE);
        rsxAddressToOffset(fb_ptr[i], &fb_offset[i]);
        gcmSetDisplayBuffer(i, fb_offset[i], PITCH, SCREEN_W, SCREEN_H);
        memset(fb_ptr[i], 0, FB_SIZE);
    }
}

void renderer_draw_frame(gcmContextData *ctx, const BlockGrid *g, const ScanState *s, uint32_t fc, int report_saved) {
    draw_rect(ctx, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
    render_header(ctx, s, fc);
    render_grid(ctx, g, s, fc);
    
    // FIX AQUI: Passando os dois argumentos agora
    render_stats(ctx, s); 
    
    if (report_saved) render_text(ctx, STATS_X, SCREEN_H-50, "REPORT SAVED!", COL_GOOD, FONT_MEDIUM);
}

void renderer_flip(gcmContextData *ctx) {
    gcmSetFlip(ctx, cur_fb);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur_fb ^= 1;
}

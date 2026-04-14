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

/* ── FUNÇÕES DE COR E BLENDING ── */
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
    uint8_t r = (c >> 24) & 0xFF;
    uint8_t g = (c >> 16) & 0xFF;
    uint8_t b = (c >>  8) & 0xFF;
    uint8_t a = (c      ) & 0xFF;
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

/* ── RASTERIZER (DRAW_RECT) ── */
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

/* ── RENDER_TEXT (Bitmap 8x8) ── */
static void render_char(int x, int y, char ch, uint32_t rgba, int scale) {
    uint8_t code = (uint8_t)ch;
    if (code < FONT8X8_FIRST || code > FONT8X8_LAST) return;

    const uint8_t *glyph = font8x8_data[code - FONT8X8_FIRST];
    uint32_t argb = rgba_to_argb(rgba);
    uint32_t *p   = fb_ptr[cur_fb];

    // Usando 8 direto para evitar conflito de macro
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
    (void)ctx;
    if (!text) return;
    int scale  = FONT_SCALE[size];
    int char_w = 8 * scale + scale;
    int cx     = x;
    for (const char *c = text; *c; c++, cx += char_w)
        render_char(cx, y, *c, rgba, scale);
}

/* ── COMPONENTES DE UI (HEADER, GRID, STATS, ETC) ── */
static void render_header(gcmContextData *ctx, const ScanState *s, uint32_t fc) {
    draw_rect(ctx, 0, 0, SCREEN_W, 72, COL_PANEL);
    draw_rect(ctx, 0, 70, SCREEN_W, 2, COL_ACCENT);
    draw_rect(ctx, 0, 68, SCREEN_W, 1, COL_HEADER_LINE);
    render_text(ctx, 40, 14, "PS3 SafeHDD Health", COL_TEXT, FONT_LARGE);
    render_text(ctx, 40, 48, "HDD Diagnostic Tool  v1.1", COL_SUBTEXT, FONT_SMALL);

    const char *status_label;
    uint32_t    status_col;
    if (!s->scan_active && !s->scan_done) {
        status_label = "[ READY ]"; status_col = COL_SUBTEXT;
    } else if (s->scan_active) {
        status_label = blink(fc) ? "[ SCANNING ]" : "[          ]";
        status_col   = COL_ACCENT2;
    } else {
        status_label = "[ DONE ]"; status_col = COL_GOOD;
    }
    render_text(ctx, SCREEN_W - 230, 26, status_label, status_col, FONT_MEDIUM);
    render_text(ctx, SCREEN_W - 420, 50, "X:Scan  /\\:Reset  O:Report  SELECT:Exit", COL_SUBTEXT, FONT_SMALL);
}

static void render_grid(gcmContextData *ctx, const BlockGrid *g, const ScanState *s, uint32_t fc) {
    int pw = GRID_COLS * (CELL_W + CELL_GAP) + CELL_GAP + 20;
    int ph = GRID_ROWS * (CELL_H + CELL_GAP) + CELL_GAP + 34;
    draw_rect(ctx, GRID_ORIGIN_X-12, GRID_ORIGIN_Y-26, pw, ph, COL_PANEL);
    draw_rect_outline(ctx, GRID_ORIGIN_X-12, GRID_ORIGIN_Y-26, pw, ph, COL_BORDER, 1);
    render_text(ctx, GRID_ORIGIN_X-2, GRID_ORIGIN_Y-18, "BLOCK MAP", COL_ACCENT, FONT_SMALL);

    char binfo[24];
    snprintf(binfo, sizeof(binfo), "%u/%u", s->scanned, s->total);
    render_text(ctx, GRID_ORIGIN_X + pw - 80, GRID_ORIGIN_Y-18, binfo, COL_SUBTEXT, FONT_SMALL);

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int idx = row * GRID_COLS + col;
            int cx  = GRID_ORIGIN_X + col * (CELL_W + CELL_GAP);
            int cy  = GRID_ORIGIN_Y + row * (CELL_H + CELL_GAP);
            uint32_t c = status_color(g->cells[row][col]);
            if (s->scan_active && idx == (int)s->current_index) {
                if (blink(fc)) {
                    draw_rect(ctx, cx-2, cy-2, CELL_W+4, CELL_H+4, 0xFFFFFF55);
                    draw_rect(ctx, cx, cy, CELL_W, CELL_H, 0xFFFFFFFF);
                } else draw_rect(ctx, cx, cy, CELL_W, CELL_H, c);
                continue;
            }
            draw_rect(ctx, cx, cy, CELL_W, CELL_H, c);
            if (g->cells[row][col] != BLOCK_UNSCANNED) draw_rect(ctx, cx+1, cy+1, CELL_W-2, 2, brighten(c, 40));
        }
    }
}

static void render_progress(gcmContextData *ctx, const ScanState *s, uint32_t fc) {
    int bx = GRID_ORIGIN_X - 12;
    int by = GRID_ORIGIN_Y + GRID_ROWS * (CELL_H + CELL_GAP) + CELL_GAP + 14;
    int bw = GRID_COLS * (CELL_W + CELL_GAP) + 20;
    int bh = 18;
    render_text(ctx, bx, by-14, "PROGRESS", COL_ACCENT, FONT_SMALL);
    draw_rect(ctx, bx, by, bw, bh, COL_UNSCANNED);
    draw_rect_outline(ctx, bx, by, bw, bh, COL_BORDER, 1);
    if (s->total > 0 && s->scanned > 0) {
        int fill = (int)((float)s->scanned / (float)s->total * (float)(bw-2));
        draw_rect(ctx, bx+1, by+1, fill, bh/2-1, 0x5588FFFF);
        draw_rect(ctx, bx+1, by+bh/2, fill, bh/2-1, COL_ACCENT);
        if (s->scan_active && blink(fc)) draw_rect(ctx, bx+fill, by, 3, bh, COL_SCAN_CURSOR);
    }
    char txt[32];
    uint32_t pct = s->total ? (s->scanned * 100 / s->total) : 0;
    snprintf(txt, sizeof(txt), "%u/%u (%u%%)", s->scanned, s->total, pct);
    render_text(ctx, bx+bw+10, by+3, txt, COL_TEXT, FONT_SMALL);
}

static void render_stats(gcmContextData *ctx, const ScanState *s) {
    int sx = STATS_X, sy = STATS_Y;
    int pw = 370;
    draw_rect(ctx, sx-12, sy-12, pw, 300, COL_PANEL);
    draw_rect_outline(ctx, sx-12, sy-12, pw, 300, COL_BORDER, 1);
    draw_rect(ctx, sx-12, sy-12, pw, 24, COL_ACCENT);
    render_text(ctx, sx, sy-8, "SCAN STATISTICS", 0x000000FF, FONT_SMALL);
    sy += 22;
    uint32_t vals[] = {s->total, s->scanned, s->good_count, s->warn_count, s->bad_count};
    const char *lbls[] = {"TOTAL BLOCKS", "SCANNED", "GOOD", "WARNING", "BAD"};
    uint32_t cols[] = {COL_TEXT, COL_TEXT, COL_GOOD, COL_WARN, COL_BAD};
    for (int i = 0; i < 5; i++) {
        draw_rect(ctx, sx-12, sy-1, pw, 1, COL_BORDER);
        char v[16]; snprintf(v, sizeof(v), "%u", vals[i]);
        render_text(ctx, sx, sy+4, lbls[i], COL_SUBTEXT, FONT_SMALL);
        render_text(ctx, sx+pw-70, sy+4, v, cols[i], FONT_MEDIUM);
        sy += 28;
    }
    sy += 18;
    render_text(ctx, sx, sy, "DRIVE HEALTH", COL_ACCENT, FONT_SMALL);
    sy += 18;
    int hw = pw-24;
    draw_rect(ctx, sx, sy, hw, 20, COL_UNSCANNED);
    if (s->scanned > 0) {
        int gw = (s->good_count * hw) / s->scanned;
        int ww = (s->warn_count * hw) / s->scanned;
        draw_rect(ctx, sx, sy+1, gw, 18, COL_GOOD);
        draw_rect(ctx, sx+gw, sy+1, ww, 18, COL_WARN);
        draw_rect(ctx, sx+gw+ww, sy+1, hw-gw-ww, 18, COL_BAD);
        uint32_t hp = (s->good_count * 100) / s->scanned;
        char hs[32]; snprintf(hs, sizeof(hs), "Health Index: %u%%", hp);
        render_text(ctx, sx, sy+30, hs, hp > 80 ? COL_GOOD : COL_WARN, FONT_MEDIUM);
    }
}

static void render_legend(gcmContextData *ctx) {
    int lx = STATS_X, ly = STATS_Y + 316;
    draw_rect(ctx, lx-12, ly-12, 370, 78, COL_PANEL);
    draw_rect_outline(ctx, lx-12, ly-12, 370, 78, COL_BORDER, 1);
    render_text(ctx, lx, ly-4, "LEGEND", COL_ACCENT, FONT_SMALL);
    uint32_t cs[] = {COL_GOOD, COL_WARN, COL_BAD, COL_UNSCANNED};
    const char *ls[] = {"Good <150ms", "Warn <400ms", "Bad/No Read", "Not Scanned"};
    for (int i = 0; i < 4; i++) {
        int ix = lx + (i%2)*(370/2), iy = ly+14+(i/2)*20;
        draw_rect(ctx, ix, iy, 14, 14, cs[i]);
        render_text(ctx, ix+18, iy+1, ls[i], COL_TEXT, FONT_SMALL);
    }
}

static void render_block_info(gcmContextData *ctx, const ScanState *s, int report_saved) {
    int ix = STATS_X, iy = STATS_Y + 400;
    if (report_saved) {
        draw_rect(ctx, ix-12, iy-12, 370, 60, 0x003322FF);
        render_text(ctx, ix, iy-4, "REPORT SAVED", COL_GOOD, FONT_SMALL);
        render_text(ctx, ix, iy+14, "/dev_hdd0/PS3SHH_report.txt", COL_TEXT, FONT_SMALL);
    } else if (s->scan_active && s->current_index >= 0) {
        draw_rect(ctx, ix-12, iy-12, 370, 60, COL_PANEL2);
        render_text(ctx, ix, iy-4, "READING NOW", COL_ACCENT2, FONT_SMALL);
        char pd[40]; snprintf(pd, sizeof(pd), "%.39s", s->blocks[s->current_index].path);
        render_text(ctx, ix, iy+14, pd, COL_TEXT, FONT_SMALL);
    }
}

/* ── RENDERER INIT (720p Fix) ── */
void renderer_init(void) {
    videoState vstate;
    videoGetState(0, 0, &vstate);
    videoConfiguration vcfg;
    memset(&vcfg, 0, sizeof(vcfg));
    
    // Corrigido para VIDEO_RESOLUTION_720
    vcfg.resolution = VIDEO_RESOLUTION_720;
    vcfg.format     = VIDEO_BUFFER_FORMAT_XRGB;
    vcfg.pitch      = PITCH;
    vcfg.aspect     = VIDEO_ASPECT_16_9;
    videoConfigure(0, &vcfg, NULL, 0);

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        fb_ptr[i] = (uint32_t *)rsxMemalign(64, FB_SIZE);
        rsxAddressToOffset(fb_ptr[i], &fb_offset[i]);
        gcmSetDisplayBuffer(i, fb_offset[i], PITCH, SCREEN_W, SCREEN_H);
        memset(fb_ptr[i], 0, FB_SIZE);
    }
    cur_fb = 0;
}

void renderer_draw_frame(gcmContextData *ctx, const BlockGrid *g, const ScanState *s, uint32_t fc, int report_saved) {
    draw_rect(ctx, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
    render_header(ctx, s, fc);
    render_grid(ctx, g, s, fc);
    render_progress(ctx, s, fc);
    render_stats(s);
    render_legend(ctx);
    render_block_info(ctx, s, report_saved);
}

void renderer_flip(gcmContextData *ctx) {
    gcmSetFlip(ctx, cur_fb);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur_fb ^= 1;
}

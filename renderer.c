#include "renderer.h"
#include "font8x8.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

/* ────────────────────────────────────────────────────────────────────────────
 * FRAMEBUFFER
 *
 * Dois buffers XRGB8 1280×720 alocados via rsxMemalign (VRAM RSX).
 * draw_rect() escreve direto nos pixels — software rasterizer.
 * renderer_flip() faz gcmSetFlip para apresentar o buffer na tela.
 *
 * Pixel format: ARGB8  (0xAARRGGBB)
 * Header usa:   RGBA8  (0xRRGGBBAA) — convertido em rgba_to_argb()
 * ────────────────────────────────────────────────────────────────────────── */

#define RSX_FB_COUNT  2
#define PITCH         (SCREEN_W * 4)
#define FB_SIZE       (PITCH * SCREEN_H)

static uint32_t *fb_ptr   [RSX_FB_COUNT];
static uint32_t  fb_offset[RSX_FB_COUNT];
static int       cur_fb   = 0;

typedef enum { FONT_SMALL = 0, FONT_MEDIUM, FONT_LARGE } FontSize;
static const int FONT_SCALE[] = { 1, 2, 3 };

/* ────────────────────────────────────────────────────────────────────────── */
/*  COR                                                                       */
/* ────────────────────────────────────────────────────────────────────────── */

static inline uint32_t rgba_to_argb(uint32_t rgba) {
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    uint8_t a = (rgba      ) & 0xFF;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g <<  8) | b;
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
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) |
           ((uint32_t)b <<  8) | a;
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

/* ────────────────────────────────────────────────────────────────────────── */
/*  DRAW_RECT — software rasterizer                                           */
/* ────────────────────────────────────────────────────────────────────────── */

static void draw_rect(gcmContextData *ctx,
                      int x, int y, int w, int h, uint32_t rgba) {
    (void)ctx;
    if (w <= 0 || h <= 0) return;

    int x1 = (x < 0)         ? 0        : x;
    int y1 = (y < 0)         ? 0        : y;
    int x2 = (x + w > SCREEN_W) ? SCREEN_W : x + w;
    int y2 = (y + h > SCREEN_H) ? SCREEN_H : y + h;
    if (x1 >= x2 || y1 >= y2) return;

    uint32_t argb = rgba_to_argb(rgba);
    uint32_t *p   = fb_ptr[cur_fb];
    uint8_t   a   = rgba & 0xFF;

    if (a == 0xFF) {
        for (int row = y1; row < y2; row++) {
            uint32_t *line = p + row * SCREEN_W + x1;
            for (int col = 0, len = x2 - x1; col < len; col++)
                line[col] = argb;
        }
    } else {
        for (int row = y1; row < y2; row++) {
            uint32_t *line = p + row * SCREEN_W + x1;
            for (int col = 0, len = x2 - x1; col < len; col++)
                line[col] = blend_pixel(line[col], argb);
        }
    }
}

static void draw_rect_outline(gcmContextData *ctx,
                               int x, int y, int w, int h,
                               uint32_t rgba, int t) {
    draw_rect(ctx, x,         y,         w, t, rgba);
    draw_rect(ctx, x,         y + h - t, w, t, rgba);
    draw_rect(ctx, x,         y,         t, h, rgba);
    draw_rect(ctx, x + w - t, y,         t, h, rgba);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  RENDER_TEXT — bitmap font 8×8 escalável                                  */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_char(int x, int y, char ch, uint32_t rgba, int scale) {
    uint8_t code = (uint8_t)ch;
    if (code < FONT8X8_FIRST || code > FONT8X8_LAST) return;

    const uint8_t *glyph = font8x8_data[code - FONT8X8_FIRST];
    uint32_t argb = rgba_to_argb(rgba);
    uint32_t *p   = fb_ptr[cur_fb];

    for (int gy = 0; gy < FONT8X8_H; gy++) {
        uint8_t bits = glyph[gy];
        for (int gx = 0; gx < FONT8X8_W; gx++) {
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

static void render_text(gcmContextData *ctx,
                         int x, int y, const char *text,
                         uint32_t rgba, FontSize size) {
    (void)ctx;
    if (!text) return;
    int scale  = FONT_SCALE[size];
    int char_w = FONT8X8_W * scale + scale;   /* +scale = kerning */
    int cx     = x;
    for (const char *c = text; *c; c++, cx += char_w)
        render_char(cx, y, *c, rgba, scale);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  HEADER                                                                    */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_header(gcmContextData *ctx, const ScanState *s,
                           uint32_t fc) {
    draw_rect(ctx, 0, 0, SCREEN_W, 72, COL_PANEL);
    draw_rect(ctx, 0, 70, SCREEN_W, 2, COL_ACCENT);
    draw_rect(ctx, 0, 68, SCREEN_W, 1, COL_HEADER_LINE);

    render_text(ctx, 40, 14, "PS3 SafeHDD Health", COL_TEXT,    FONT_LARGE);
    render_text(ctx, 40, 48, "HDD Diagnostic Tool  v1.1",
                COL_SUBTEXT, FONT_SMALL);

    const char *status_label;
    uint32_t    status_col;
    if (!s->scan_active && !s->scan_done) {
        status_label = "[ READY ]";    status_col = COL_SUBTEXT;
    } else if (s->scan_active) {
        status_label = blink(fc) ? "[ SCANNING ]" : "[          ]";
        status_col   = COL_ACCENT2;
    } else {
        status_label = "[ DONE ]";     status_col = COL_GOOD;
    }
    render_text(ctx, SCREEN_W - 230, 26, status_label, status_col, FONT_MEDIUM);
    render_text(ctx, SCREEN_W - 420, 50,
                "X:Scan  /\\:Reset  O:Report  SELECT:Exit",
                COL_SUBTEXT, FONT_SMALL);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  GRID                                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_grid(gcmContextData *ctx, const BlockGrid *g,
                         const ScanState *s, uint32_t fc) {
    int pw = GRID_COLS * (CELL_W + CELL_GAP) + CELL_GAP + 20;
    int ph = GRID_ROWS * (CELL_H + CELL_GAP) + CELL_GAP + 34;

    draw_rect(ctx, GRID_ORIGIN_X-12, GRID_ORIGIN_Y-26, pw, ph, COL_PANEL);
    draw_rect_outline(ctx, GRID_ORIGIN_X-12, GRID_ORIGIN_Y-26,
                      pw, ph, COL_BORDER, 1);

    render_text(ctx, GRID_ORIGIN_X-2, GRID_ORIGIN_Y-18,
                "BLOCK MAP", COL_ACCENT, FONT_SMALL);

    char binfo[24];
    snprintf(binfo, sizeof(binfo), "%u/%u", s->scanned, s->total);
    render_text(ctx, GRID_ORIGIN_X + pw - 80, GRID_ORIGIN_Y-18,
                binfo, COL_SUBTEXT, FONT_SMALL);

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int      idx = row * GRID_COLS + col;
            int      cx  = GRID_ORIGIN_X + col * (CELL_W + CELL_GAP);
            int      cy  = GRID_ORIGIN_Y + row * (CELL_H + CELL_GAP);
            uint32_t c   = status_color(g->cells[row][col]);

            if (s->scan_active && idx == s->current_index) {
                if (blink(fc)) {
                    draw_rect(ctx, cx-2, cy-2, CELL_W+4, CELL_H+4, 0xFFFFFF55);
                    draw_rect(ctx, cx,   cy,   CELL_W,   CELL_H,   0xFFFFFFFF);
                } else {
                    draw_rect(ctx, cx, cy, CELL_W, CELL_H, c);
                }
                continue;
            }
            draw_rect(ctx, cx, cy, CELL_W, CELL_H, c);
            if (g->cells[row][col] != BLOCK_UNSCANNED)
                draw_rect(ctx, cx+1, cy+1, CELL_W-2, 2, brighten(c, 40));
        }
    }
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  PROGRESSO                                                                 */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_progress(gcmContextData *ctx, const ScanState *s,
                              uint32_t fc) {
    int bx = GRID_ORIGIN_X - 12;
    int by = GRID_ORIGIN_Y + GRID_ROWS * (CELL_H + CELL_GAP) + CELL_GAP + 14;
    int bw = GRID_COLS * (CELL_W + CELL_GAP) + 20;
    int bh = 18;

    render_text(ctx, bx, by-14, "PROGRESS", COL_ACCENT, FONT_SMALL);
    draw_rect(ctx, bx, by, bw, bh, COL_UNSCANNED);
    draw_rect_outline(ctx, bx, by, bw, bh, COL_BORDER, 1);

    if (s->total > 0 && s->scanned > 0) {
        int fill = (int)((float)s->scanned / (float)s->total * (float)(bw-2));
        if (fill < 1) fill = 1;
        draw_rect(ctx, bx+1, by+1,    fill, bh/2-1, 0x5588FFFF);
        draw_rect(ctx, bx+1, by+bh/2, fill, bh/2-1, COL_ACCENT);
        if (s->scan_active && blink(fc))
            draw_rect(ctx, bx+fill, by, 3, bh, COL_SCAN_CURSOR);
    }

    char txt[32];
    uint32_t pct = s->total ? (s->scanned * 100 / s->total) : 0;
    snprintf(txt, sizeof(txt), "%u/%u (%u%%)", s->scanned, s->total, pct);
    render_text(ctx, bx+bw+10, by+3, txt, COL_TEXT, FONT_SMALL);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  ESTATÍSTICAS                                                              */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_stats(gcmContextData *ctx, const ScanState *s) {
    int sx = STATS_X, sy = STATS_Y;
    int pw = 370;

    draw_rect(ctx, sx-12, sy-12, pw, 300, COL_PANEL);
    draw_rect_outline(ctx, sx-12, sy-12, pw, 300, COL_BORDER, 1);
    draw_rect(ctx, sx-12, sy-12, pw, 24, COL_ACCENT);
    render_text(ctx, sx, sy-8, "SCAN STATISTICS", 0x000000FF, FONT_SMALL);
    sy += 22;

    typedef struct { const char *label; uint32_t val; uint32_t col; } Row;
    Row rows[] = {
        { "TOTAL BLOCKS", s->total,      COL_TEXT },
        { "SCANNED",      s->scanned,    COL_TEXT },
        { "GOOD",         s->good_count, COL_GOOD },
        { "WARNING",      s->warn_count, COL_WARN },
        { "BAD",          s->bad_count,  COL_BAD  },
    };
    for (int i = 0; i < 5; i++) {
        draw_rect(ctx, sx-12, sy-1, pw, 1, COL_BORDER);
        char val[16];
        snprintf(val, sizeof(val), "%u", rows[i].val);
        render_text(ctx, sx,       sy+4, rows[i].label, COL_SUBTEXT, FONT_SMALL);
        render_text(ctx, sx+pw-70, sy+4, val,           rows[i].col, FONT_MEDIUM);
        sy += 28;
    }
    sy += 10;
    draw_rect(ctx, sx-12, sy-1, pw, 1, COL_BORDER);
    sy += 8;

    render_text(ctx, sx, sy, "DRIVE HEALTH", COL_ACCENT, FONT_SMALL);
    sy += 18;

    int hw = pw-24, hh = 20;
    draw_rect(ctx, sx, sy, hw, hh, COL_UNSCANNED);
    draw_rect_outline(ctx, sx, sy, hw, hh, COL_BORDER, 1);

    if (s->scanned > 0) {
        int gw = (int)((float)s->good_count / (float)s->scanned * hw);
        int ww = (int)((float)s->warn_count / (float)s->scanned * hw);
        int bw = hw - gw - ww;
        if (gw > 0) draw_rect(ctx, sx,       sy+1, gw, hh-2, COL_GOOD);
        if (ww > 0) draw_rect(ctx, sx+gw,    sy+1, ww, hh-2, COL_WARN);
        if (bw > 0) draw_rect(ctx, sx+gw+ww, sy+1, bw, hh-2, COL_BAD);
        draw_rect(ctx, sx+1, sy+1, hw-2, 4, 0xFFFFFF22);
        sy += hh+10;

        char hs[32];
        uint32_t hp = s->good_count * 100 / s->scanned;
        uint32_t hc = hp > 80 ? COL_GOOD : hp > 50 ? COL_WARN : COL_BAD;
        snprintf(hs, sizeof(hs), "Health Index: %u%%", hp);
        render_text(ctx, sx, sy, hs, hc, FONT_MEDIUM);
    }
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  LEGENDA                                                                   */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_legend(gcmContextData *ctx) {
    int lx = STATS_X, ly = STATS_Y + 316;
    int pw = 370;

    draw_rect(ctx, lx-12, ly-12, pw, 78, COL_PANEL);
    draw_rect_outline(ctx, lx-12, ly-12, pw, 78, COL_BORDER, 1);
    render_text(ctx, lx, ly-4, "LEGEND", COL_ACCENT, FONT_SMALL);

    typedef struct { uint32_t c; const char *label; } Li;
    Li items[] = {
        { COL_GOOD,      "Good  <150ms" },
        { COL_WARN,      "Warn  <400ms" },
        { COL_BAD,       "Bad/No Read"  },
        { COL_UNSCANNED, "Not Scanned"  },
    };
    int ix = lx, iy = ly+14;
    for (int i = 0; i < 4; i++) {
        draw_rect(ctx, ix, iy, 14, 14, items[i].c);
        draw_rect_outline(ctx, ix, iy, 14, 14, COL_BORDER, 1);
        render_text(ctx, ix+18, iy+1, items[i].label, COL_TEXT, FONT_SMALL);
        ix += pw/2;
        if (i == 1) { ix = lx; iy += 20; }
    }
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  INFO DO BLOCO ATUAL / NOTIFICAÇÃO DE RELATÓRIO                            */
/* ────────────────────────────────────────────────────────────────────────── */

static void render_block_info(gcmContextData *ctx,
                               const ScanState *s, int report_saved) {
    int ix = STATS_X, iy = STATS_Y + 316 + 84;
    int pw = 370;

    if (report_saved) {
        draw_rect(ctx, ix-12, iy-12, pw, 60, 0x003322FF);
        draw_rect_outline(ctx, ix-12, iy-12, pw, 60, COL_GOOD, 1);
        render_text(ctx, ix, iy-4,  "REPORT SAVED", COL_GOOD, FONT_SMALL);
        render_text(ctx, ix, iy+14, "/dev_hdd0/PS3SHH_report.txt",
                    COL_TEXT, FONT_SMALL);
        return;
    }
    if (!s->scan_active || s->current_index < 0) return;

    draw_rect(ctx, ix-12, iy-12, pw, 60, COL_PANEL2);
    draw_rect_outline(ctx, ix-12, iy-12, pw, 60, COL_ACCENT, 1);
    render_text(ctx, ix, iy-4, "READING NOW", COL_ACCENT2, FONT_SMALL);

    const BlockResult *cur = &s->blocks[s->current_index];
    char pd[40];
    snprintf(pd, sizeof(pd), "%.39s", cur->path);
    render_text(ctx, ix, iy+14, pd, COL_TEXT, FONT_SMALL);

    if (cur->latency_ms > 0) {
        char lat[32];
        snprintf(lat, sizeof(lat), "Latency: %u ms", cur->latency_ms);
        render_text(ctx, ix, iy+30, lat, COL_SUBTEXT, FONT_SMALL);
    }
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  INIT                                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

void renderer_init(void) {
    videoState vstate;
    videoGetState(0, 0, &vstate);

    videoConfiguration vcfg;
    memset(&vcfg, 0, sizeof(vcfg));
    vcfg.resolution = VIDEO_RESOLUTION_1280x720;
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

/* ────────────────────────────────────────────────────────────────────────── */
/*  FRAME COMPLETO                                                            */
/* ────────────────────────────────────────────────────────────────────────── */

void renderer_draw_frame(gcmContextData *ctx,
                          const BlockGrid *g,
                          const ScanState *s,
                          uint32_t         fc,
                          int              report_saved) {
    draw_rect(ctx, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
    render_header    (ctx, s, fc);
    render_grid      (ctx, g, s, fc);
    render_progress  (ctx, s, fc);
    render_stats     (ctx, s);
    render_legend    (ctx);
    render_block_info(ctx, s, report_saved);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  FLIP                                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

void renderer_flip(gcmContextData *ctx) {
    gcmSetFlip(ctx, cur_fb);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur_fb ^= 1;
}

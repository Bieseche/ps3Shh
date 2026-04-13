#ifndef RENDERER_H
#define RENDERER_H

#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include "scanner.h"
#include "mapper.h"

/* ── Resolução ──────────────────────────────────────────────────────────── */
#define SCREEN_W  1280
#define SCREEN_H   720

/* ── Paleta RGBA8 (0xRRGGBBAA) ──────────────────────────────────────────── */
#define COL_BG          0x0A0A14FF
#define COL_PANEL       0x10101EFF
#define COL_PANEL2      0x14142AFF
#define COL_BORDER      0x1E1E3CFF
#define COL_ACCENT      0x3D6AFFFF
#define COL_ACCENT2     0x00C8FFFF
#define COL_GOOD        0x00E676FF
#define COL_WARN        0xFFAB00FF
#define COL_BAD         0xFF1744FF
#define COL_UNSCANNED   0x22223AFF
#define COL_TEXT        0xDDE4FFFF
#define COL_SUBTEXT     0x6677AAFF
#define COL_SCAN_CURSOR 0xFFFFFFFF
#define COL_HEADER_LINE 0x3D6AFF55

/* ── Layout grid ────────────────────────────────────────────────────────── */
#define GRID_ORIGIN_X    52
#define GRID_ORIGIN_Y   112
#define CELL_W           28
#define CELL_H           28
#define CELL_GAP          3

/* ── Layout painel stats ────────────────────────────────────────────────── */
#define STATS_X         600
#define STATS_Y         112

/* ── API ────────────────────────────────────────────────────────────────── */
void renderer_init      (void);

/*
 * report_saved: 1 = exibe notificação de relatório exportado
 *               0 = comportamento normal
 */
void renderer_draw_frame(gcmContextData *ctx,
                          const BlockGrid *g,
                          const ScanState *s,
                          uint32_t         frame_counter,
                          int              report_saved);

void renderer_flip      (gcmContextData *ctx);

#endif /* RENDERER_H */

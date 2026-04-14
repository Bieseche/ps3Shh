/*
 * PS3 SafeHDD Health — v1.1
 * HDD Diagnostic Tool para PS3 CFW (PSL1GHT / ps3dev toolchain)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

#include "scanner.h"
#include "mapper.h"
#include "renderer.h"
#include "sound.h"
#include "report.h"
#include "saferead.h"

/* ── RSX setup ──────────────────────────────────────────────────────────── */
#define HOST_SIZE  (32 * 1024 * 1024)
#define CB_SIZE    (512 * 1024)

static gcmContextData *rsx_ctx = NULL;

static void rsx_init(void) {
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&rsx_ctx, CB_SIZE, HOST_SIZE, host);
    renderer_init();
}

/* ── Sysutil callback ───────────────────────────────────────────────────── */
static volatile int quit_flag = 0;

static void sysutil_cb(uint64_t status, uint64_t param, void *ud) {
    (void)param; (void)ud;
    if (status == SYSUTIL_EXIT_GAME) quit_flag = 1;
}

/* ── Pad com debounce ───────────────────────────────────────────────────── */
typedef struct { uint32_t held, prev; } PadState;

static inline int pad_pressed(PadState *p, uint32_t btn) {
    return (p->held & btn) && !(p->prev & btn);
}

/* ── Definições de Botões (Padrão PSL1GHT / Libpad) ─────────────────────── */
// Usando as máscaras de bits diretas para evitar erro de 'undeclared'
#define BTN_SELECT   0x0100
#define BTN_CROSS    0x4000
#define BTN_CIRCLE   0x2000
#define BTN_TRIANGLE 0x1000

/* ── Notificação de relatório (exibe por N frames) ──────────────────────── */
#define REPORT_NOTIFY_FRAMES  180 

/* ── Loop principal ─────────────────────────────────────────────────────── */
int main(void) {
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    ioPadInit(7);
    rsx_init();
    sound_init();

    ScanState scan;
    BlockGrid grid;
    scanner_init(&scan, MAX_BLOCKS);
    mapper_build(&grid, &scan);

    padInfo  pad_info;
    padData  pad_data;
    PadState pad = { 0, 0 };

    uint32_t frame_counter   = 0;
    int      scan_index      = 0;
    int      report_notify   = 0;

    while (!quit_flag) {
        sysUtilCheckCallback();

        /* ── Input ── */
        pad.prev = pad.held;
        pad.held = 0;
        
        ioPadGetInfo(&pad_info);
        if (pad_info.status[0]) {
            ioPadGetData(0, &pad_data);
            // Pegando o estado dos botões digitais (offset 2 e 3)
            pad.held = (uint32_t)pad_data.button[2] | ((uint32_t)pad_data.button[3] << 8);
        }

        /* X → inicia scan */
        if (pad_pressed(&pad, BTN_CROSS) &&
            !scan.scan_active && !scan.scan_done) {
            scan.scan_active   = 1;
            scan.current_index = 0;
            scan_index         = 0;
        }

        /* Triângulo → reset */
        if (pad_pressed(&pad, BTN_TRIANGLE)) {
            scanner_reset(&scan);
            mapper_build(&grid, &scan);
            scan_index    = 0;
            report_notify = 0;
        }

        /* O → exporta relatório */
        if (pad_pressed(&pad, BTN_CIRCLE) && scan.scan_done) {
            if (report_export(&scan) == 0) {
                report_notify = REPORT_NOTIFY_FRAMES;
            }
        }

        /* SELECT → sair */
        if (pad_pressed(&pad, BTN_SELECT)) break;

        /* ── Scan incremental ── */
        if (scan.scan_active && scan_index < (int)scan.total) {
            scan.current_index = scan_index;
            scanner_scan_block(&scan, (uint32_t)scan_index);

            if (scan.blocks[scan_index].status == BLOCK_BAD)
                sound_beep_bad();

            mapper_build(&grid, &scan);
            scan_index++;

            if (scan_index >= (int)scan.total) {
                scan.scan_active   = 0;
                scan.scan_done     = 1;
                scan.current_index = -1;
                sound_beep_done();
            }
        }

        if (report_notify > 0) report_notify--;

        /* ── Render ── */
        renderer_draw_frame(rsx_ctx, &grid, &scan,
                            frame_counter, report_notify > 0);
        renderer_flip(rsx_ctx);

        frame_counter++;
    }

    sound_shutdown();
    ioPadEnd();
    return 0;
}

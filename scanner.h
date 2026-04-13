#ifndef SCANNER_H
#define SCANNER_H

#include <stdint.h>
#include <sys/time.h>

/* ── Configuração de blocos ─────────────────────────────────────────────── */
#define MAX_BLOCKS      256
#define BLOCK_SIZE      (64 * 1024)   /* 64 KB por bloco lógico            */
#define WARN_LATENCY_MS  150          /* acima disso → WARNING              */
#define BAD_LATENCY_MS   400          /* acima disso → BAD                  */

/* Caminhos reais do PS3 HDD que serão escaneados */
#define SCAN_BASE_PATH  "/dev_hdd0"
#define SCAN_PATHS_COUNT 8

static const char *SCAN_PATHS[SCAN_PATHS_COUNT] = {
    "/dev_hdd0/game",
    "/dev_hdd0/savedata",
    "/dev_hdd0/home",
    "/dev_hdd0/vsh",
    "/dev_hdd0/mms",
    "/dev_hdd0/video",
    "/dev_hdd0/music",
    "/dev_hdd0/photo"
};

/* ── Tipos ──────────────────────────────────────────────────────────────── */
typedef enum {
    BLOCK_UNSCANNED = 0,
    BLOCK_GOOD      = 1,
    BLOCK_WARNING   = 2,
    BLOCK_BAD       = 3
} BlockStatus;

typedef struct {
    uint32_t    index;
    BlockStatus status;
    uint32_t    latency_ms;
    uint8_t     read_ok;
    char        path[128];     /* qual arquivo/diretório foi lido         */
} BlockResult;

typedef struct {
    BlockResult blocks[MAX_BLOCKS];
    uint32_t    total;
    uint32_t    scanned;
    uint32_t    good_count;
    uint32_t    warn_count;
    uint32_t    bad_count;
    int32_t     current_index; /* bloco sendo lido agora, -1 = idle       */
    uint8_t     scan_active;
    uint8_t     scan_done;
} ScanState;

/* ── API pública ────────────────────────────────────────────────────────── */
void scanner_init       (ScanState *s, uint32_t total_blocks);
void scanner_scan_block (ScanState *s, uint32_t index);
void scanner_reset      (ScanState *s);

/* Retorna timestamp em milissegundos (monotônico) */
uint64_t scanner_time_ms(void);

#endif /* SCANNER_H */

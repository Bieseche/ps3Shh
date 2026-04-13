#include "saferead.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* Reutiliza a função de timestamp do scanner */
extern uint64_t scanner_time_ms(void);

/* ── Predicado de segurança ─────────────────────────────────────────────── */

int saferead_is_safe(const ScanState *s, uint32_t block_index) {
    if (block_index >= s->total) return 0;

    const BlockResult *b = &s->blocks[block_index];

    /* Bloco não escaneado ainda → assume seguro (leitura otimista) */
    if (b->status == BLOCK_UNSCANNED) return 1;

    /* BAD conhecido → não é seguro */
    if (b->status == BLOCK_BAD) return 0;

    /* GOOD ou WARNING → seguro */
    return 1;
}

uint32_t saferead_available_blocks(const ScanState *s) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->scanned; i++) {
        if (s->blocks[i].status != BLOCK_BAD) count++;
    }
    return count;
}

/* ── Leitura protegida ──────────────────────────────────────────────────── */

SafeReadResult saferead_block(const ScanState *s,
                               uint32_t         block_index,
                               void            *buf,
                               size_t           buf_size) {
    SafeReadResult result;
    memset(&result, 0, sizeof(result));
    result.block_index = block_index;

    /* Validação de range */
    if (block_index >= s->total) {
        result.status = SR_INVALID;
        return result;
    }

    /* Desvio de bloco BAD — zero I/O */
    if (!saferead_is_safe(s, block_index)) {
        result.status = SR_SKIPPED;
        return result;
    }

    /* O path real já foi resolvido durante o scan anterior */
    const BlockResult *b = &s->blocks[block_index];
    const char *path = b->path;

    if (path[0] == '\0') {
        result.status = SR_ERROR;
        return result;
    }

    /* Abre o diretório e lê o primeiro arquivo válido */
    uint64_t t0 = scanner_time_ms();

    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Tenta como diretório → lê o primeiro arquivo dentro */
        /* (mesmo comportamento do scanner_scan_block) */
        result.status    = SR_ERROR;
        result.latency_ms = (uint32_t)(scanner_time_ms() - t0);
        return result;
    }

    size_t n = fread(buf, 1, buf_size, f);
    result.latency_ms = (uint32_t)(scanner_time_ms() - t0);
    fclose(f);

    if (n == 0) {
        result.status = SR_ERROR;
        return result;
    }

    result.status     = SR_OK;
    result.bytes_read = (uint32_t)n;
    return result;
}

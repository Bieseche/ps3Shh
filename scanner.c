#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* ── Utilitários ────────────────────────────────────────────────────────── */

uint64_t scanner_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec) / 1000ULL;
}

/* ── Init / Reset ───────────────────────────────────────────────────────── */

void scanner_init(ScanState *s, uint32_t total_blocks) {
    memset(s, 0, sizeof(ScanState));
    s->total         = (total_blocks > MAX_BLOCKS) ? MAX_BLOCKS : total_blocks;
    s->current_index = -1;
    s->scan_active   = 0;
    s->scan_done     = 0;
}

void scanner_reset(ScanState *s) {
    uint32_t total = s->total;
    scanner_init(s, total);
}

/* ── Leitura real de um bloco ───────────────────────────────────────────── */
/*
 * Estratégia de mapeamento bloco → arquivo real:
 *
 *  O HDD do PS3 é dividido logicamente em SCAN_PATHS_COUNT grupos.
 *  Cada grupo mapeia para um subdiretório real (/dev_hdd0/game, etc.).
 *  Dentro de cada grupo listamos arquivos via readdir() e lemos
 *  BLOCK_SIZE bytes deles, medindo a latência real de I/O.
 *
 *  Se o diretório não existir ou não tiver arquivos legíveis,
 *  o bloco é classificado como BAD (read_ok = 0).
 *
 *  Isso garante leitura REAL do filesystem PS3 — sem fopen de
 *  arquivos sintéticos, sem dados inventados.
 */

static void classify(BlockResult *b) {
    if (!b->read_ok || b->latency_ms > BAD_LATENCY_MS)
        b->status = BLOCK_BAD;
    else if (b->latency_ms > WARN_LATENCY_MS)
        b->status = BLOCK_WARNING;
    else
        b->status = BLOCK_GOOD;
}

/*
 * Tenta ler BLOCK_SIZE bytes de um arquivo real dentro de `dir_path`.
 * Itera pelos arquivos do diretório até encontrar um legível.
 * Retorna 1 se leu com sucesso, 0 se falhou.
 */
static int read_block_from_dir(const char *dir_path, uint32_t *latency_out) {
    DIR *d = opendir(dir_path);
    if (!d) {
        *latency_out = BAD_LATENCY_MS + 1;
        return 0;
    }

    uint8_t *buf = (uint8_t *)malloc(BLOCK_SIZE);
    if (!buf) {
        closedir(d);
        *latency_out = BAD_LATENCY_MS + 1;
        return 0;
    }

    struct dirent *entry;
    int success = 0;

    while ((entry = readdir(d)) != NULL) {
        /* Ignora . e .. */
        if (entry->d_name[0] == '.') continue;

        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) != 0) continue;

        /* Só arquivos regulares com tamanho mínimo */
        if (!S_ISREG(st.st_mode)) continue;
        if (st.st_size < 512)     continue;

        FILE *f = fopen(filepath, "rb");
        if (!f) continue;

        uint64_t t0 = scanner_time_ms();
        size_t   n  = fread(buf, 1, BLOCK_SIZE, f);
        *latency_out = (uint32_t)(scanner_time_ms() - t0);
        fclose(f);

        /* Aceita leitura parcial desde que tenha lido algo */
        if (n > 0) {
            success = 1;
            break;
        }
    }

    closedir(d);
    free(buf);

    if (!success) {
        *latency_out = BAD_LATENCY_MS + 1;
    }

    return success;
}

/*
 * Alguns blocos mapeiam para subdiretórios dentro dos paths principais.
 * Usamos o índice do bloco para variar o diretório explorado,
 * distribuindo a cobertura pelo filesystem real.
 */
static void resolve_block_path(uint32_t index, char *path_out, size_t path_size) {
    uint32_t group    = index % SCAN_PATHS_COUNT;
    uint32_t subindex = index / SCAN_PATHS_COUNT;

    const char *base = SCAN_PATHS[group];

    if (subindex == 0) {
        /* Lê direto do diretório raiz do grupo */
        snprintf(path_out, path_size, "%s", base);
        return;
    }

    /* Tenta descer um nível: lista subpastas do grupo */
    DIR *d = opendir(base);
    if (!d) {
        snprintf(path_out, path_size, "%s", base);
        return;
    }

    struct dirent *entry;
    uint32_t sub_count = 0;

    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char subpath[256];
        snprintf(subpath, sizeof(subpath), "%s/%s", base, entry->d_name);

        struct stat st;
        if (stat(subpath, &st) != 0)    continue;
        if (!S_ISDIR(st.st_mode))        continue;

        if (sub_count == (subindex - 1) % 8) {
            snprintf(path_out, path_size, "%s", subpath);
            closedir(d);
            return;
        }
        sub_count++;
    }

    closedir(d);
    /* Fallback: usa o base */
    snprintf(path_out, path_size, "%s", base);
}

/* ── Ponto de entrada público ───────────────────────────────────────────── */

void scanner_scan_block(ScanState *s, uint32_t index) {
    if (index >= s->total) return;

    BlockResult *b = &s->blocks[index];
    memset(b, 0, sizeof(BlockResult));
    b->index = index;

    /* Resolve qual caminho real este bloco representa */
    resolve_block_path(index, b->path, sizeof(b->path));

    /* Leitura real + medição de latência */
    uint32_t latency = 0;
    int ok = read_block_from_dir(b->path, &latency);

    b->read_ok    = (uint8_t)ok;
    b->latency_ms = latency;

    classify(b);

    /* Atualiza contadores do estado global */
    switch (b->status) {
        case BLOCK_GOOD:    s->good_count++; break;
        case BLOCK_WARNING: s->warn_count++; break;
        case BLOCK_BAD:     s->bad_count++;  break;
        default: break;
    }

    s->scanned++;
}

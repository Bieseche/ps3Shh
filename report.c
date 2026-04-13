#include "report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Helpers de formatação ──────────────────────────────────────────────── */

static const char *status_str(BlockStatus s) {
    switch (s) {
        case BLOCK_GOOD:    return "GOOD   ";
        case BLOCK_WARNING: return "WARNING";
        case BLOCK_BAD:     return "BAD    ";
        default:            return "------";
    }
}

/* Linha divisória */
static void write_divider(FILE *f, char c, int n) {
    for (int i = 0; i < n; i++) fputc(c, f);
    fputc('\n', f);
}

/* ── Exportação ─────────────────────────────────────────────────────────── */

int report_export(const ScanState *s) {
    FILE *f = fopen(REPORT_PATH, "w");
    if (!f) return -1;

    /* ── Cabeçalho ── */
    write_divider(f, '=', 64);
    fprintf(f, "  PS3 SafeHDD Health — Scan Report  v1.1\n");
    fprintf(f, "  Tool: PS3SHH  |  Target: /dev_hdd0\n");
    write_divider(f, '=', 64);
    fprintf(f, "\n");

    /* Timestamp via RTC do PS3 (sys_time_get_current_time) não disponível
     * sem sysutil em contexto de report — usa contador de blocos como ref */
    fprintf(f, "  Total Blocks Scanned : %u / %u\n",
            s->scanned, s->total);
    fprintf(f, "  GOOD                 : %u\n", s->good_count);
    fprintf(f, "  WARNING              : %u\n", s->warn_count);
    fprintf(f, "  BAD                  : %u\n", s->bad_count);

    if (s->scanned > 0) {
        uint32_t health_pct = s->good_count * 100 / s->scanned;
        const char *verdict;

        if (health_pct > 80)      verdict = "HEALTHY";
        else if (health_pct > 50) verdict = "DEGRADED";
        else                      verdict = "CRITICAL — BACKUP RECOMMENDED";

        fprintf(f, "  Health Index         : %u%%  [%s]\n",
                health_pct, verdict);
    }

    fprintf(f, "\n");
    write_divider(f, '-', 64);
    fprintf(f, "  %-6s  %-7s  %8s  %s\n",
            "BLOCK", "STATUS", "LATENCY", "PATH");
    write_divider(f, '-', 64);

    /* ── Tabela de blocos ── */
    for (uint32_t i = 0; i < s->scanned; i++) {
        const BlockResult *b = &s->blocks[i];

        /* Marca blocos ruins com '!' para fácil grep */
        char flag = (b->status == BLOCK_BAD)     ? '!' :
                    (b->status == BLOCK_WARNING)  ? '*' : ' ';

        fprintf(f, "%c %-5u  %s  %6u ms  %s\n",
                flag,
                b->index,
                status_str(b->status),
                b->latency_ms,
                b->path[0] ? b->path : "(unknown)");
    }

    fprintf(f, "\n");
    write_divider(f, '=', 64);
    fprintf(f, "  ! = BAD block   * = WARNING block\n");
    fprintf(f, "  Report saved to: %s\n", REPORT_PATH);
    write_divider(f, '=', 64);

    fclose(f);
    return 0;
}

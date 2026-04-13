#ifndef REPORT_H
#define REPORT_H

#include "scanner.h"

/* Caminho de saída do relatório no HDD do PS3 */
#define REPORT_PATH "/dev_hdd0/PS3SHH_report.txt"

/*
 * Exporta o relatório completo do scan para REPORT_PATH.
 * Retorna 0 em sucesso, -1 em erro.
 */
int report_export(const ScanState *s);

#endif /* REPORT_H */

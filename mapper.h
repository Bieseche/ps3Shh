#ifndef MAPPER_H
#define MAPPER_H

#include "scanner.h"

#define GRID_COLS 16
#define GRID_ROWS 16

typedef struct {
    BlockStatus cells[GRID_ROWS][GRID_COLS];
} BlockGrid;

void mapper_build(BlockGrid *g, const ScanState *s);

/* Retorna a célula que contém o bloco de índice `idx` */
void mapper_index_to_cell(uint32_t idx, int *row, int *col);

#endif /* MAPPER_H */

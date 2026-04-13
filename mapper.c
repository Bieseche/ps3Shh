#include "mapper.h"
#include <string.h>

void mapper_build(BlockGrid *g, const ScanState *s) {
    memset(g, 0, sizeof(BlockGrid));
    for (uint32_t i = 0; i < s->total && i < GRID_ROWS * GRID_COLS; i++) {
        int row = (int)(i / GRID_COLS);
        int col = (int)(i % GRID_COLS);
        g->cells[row][col] = s->blocks[i].status;
    }
}

void mapper_index_to_cell(uint32_t idx, int *row, int *col) {
    *row = (int)(idx / GRID_COLS);
    *col = (int)(idx % GRID_COLS);
}

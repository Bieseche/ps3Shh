#ifndef SAFEREAD_H
#define SAFEREAD_H

#include "scanner.h"
#include <stddef.h>

/*
 * SafeRead — leitura com desvio automático de blocos BAD
 *
 * Usa o resultado do scan para pular blocos marcados como BAD
 * antes de tentar a leitura, evitando travamentos de I/O.
 *
 * Integração típica:
 *
 *   ScanState scan;   // já populado pelo scanner
 *
 *   uint8_t buf[64*1024];
 *   SafeReadResult r = saferead_block(&scan, 42, buf, sizeof(buf));
 *
 *   if (r.status == SR_OK)      // leitura bem-sucedida
 *   if (r.status == SR_SKIPPED) // bloco era BAD, pulado
 *   if (r.status == SR_ERROR)   // falha em bloco não-BAD (inesperado)
 */

typedef enum {
    SR_OK      = 0,   /* leitura realizada com sucesso    */
    SR_SKIPPED = 1,   /* bloco BAD conhecido — ignorado   */
    SR_ERROR   = 2,   /* erro de I/O em bloco não-BAD     */
    SR_INVALID = 3,   /* índice fora do range             */
} SafeReadStatus;

typedef struct {
    SafeReadStatus status;
    uint32_t       bytes_read;
    uint32_t       latency_ms;
    uint32_t       block_index;
} SafeReadResult;

/*
 * Tenta ler o bloco `block_index` com proteção SafeRead.
 *  - Se o bloco já foi scaneado como BAD → retorna SR_SKIPPED sem I/O
 *  - Se não foi scaneado ou é GOOD/WARN → realiza a leitura normalmente
 *  - `buf` e `buf_size` definem o destino da leitura
 */
SafeReadResult saferead_block(const ScanState *s,
                               uint32_t         block_index,
                               void            *buf,
                               size_t           buf_size);

/*
 * Conta quantos blocos podem ser lidos com segurança
 * (status != BLOCK_BAD e já scaneados)
 */
uint32_t saferead_available_blocks(const ScanState *s);

/*
 * Retorna 1 se o bloco é considerado seguro para leitura
 */
int saferead_is_safe(const ScanState *s, uint32_t block_index);

#endif /* SAFEREAD_H */

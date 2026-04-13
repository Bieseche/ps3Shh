# PS3 SafeHDD Health — v1.1

HDD Diagnostic Tool para PS3 CFW (PSL1GHT / ps3dev)

---

## Estrutura

```
ps3shh/
├── main.c        Loop principal, input, orquestração
├── scanner.c/h   Leitura real do HDD + classificação
├── mapper.c/h    Blocos → grid 16×16
├── renderer.c/h  UI gráfica (software framebuffer + bitmap font)
├── font8x8.h     Bitmap font 8×8, header-only, domínio público
├── sound.c/h     Beeps via libaudio
├── report.c/h    Exportação de relatório em texto
├── saferead.c/h  Leitura com desvio de blocos BAD
└── Makefile
```

---

## Novidades v1.1

| Item | Status |
|---|---|
| `draw_rect()` via software framebuffer | ✅ Implementado |
| `render_text()` via bitmap font 8×8 | ✅ Implementado |
| Framebuffers RSX reais (`rsxMemalign`) | ✅ Implementado |
| Alpha blend em `draw_rect()` | ✅ Implementado |
| Exportação de relatório (botão O) | ✅ Implementado |
| SafeRead com desvio de blocos BAD | ✅ Implementado |
| Notificação visual de relatório salvo | ✅ Implementado |

---

## Como o scan funciona

O scanner lê diretórios reais do `/dev_hdd0`:

```
/dev_hdd0/game      /dev_hdd0/savedata
/dev_hdd0/home      /dev_hdd0/vsh
/dev_hdd0/mms       /dev_hdd0/video
/dev_hdd0/music     /dev_hdd0/photo
```

Para cada bloco (0–255):
1. `resolve_block_path()` → qual dir/subdir representa este bloco
2. `opendir()` + `readdir()` + `stat()` → localiza arquivo real
3. `fread(64KB)` com `gettimeofday()` → mede latência real de I/O
4. Classifica: **GOOD** <150ms / **WARNING** <400ms / **BAD** falha ou >400ms

---

## Controles

| Botão | Ação |
|---|---|
| X | Iniciar scan |
| Triângulo | Reset |
| O | Exportar relatório (após scan) |
| SELECT | Sair |

---

## Relatório gerado

Salvo em `/dev_hdd0/PS3SHH_report.txt`:

```
================================================================
  PS3 SafeHDD Health — Scan Report  v1.1
================================================================
  Total Blocks Scanned : 256 / 256
  GOOD                 : 210
  WARNING              : 30
  BAD                  : 16
  Health Index         : 82%  [HEALTHY]
----------------------------------------------------------------
  BLOCK  STATUS    LATENCY  PATH
----------------------------------------------------------------
  0      GOOD        42 ms  /dev_hdd0/game
  1      GOOD        38 ms  /dev_hdd0/savedata
! 2      BAD        401 ms  /dev_hdd0/vsh
...
```

`!` = BAD  `*` = WARNING

---

## SafeRead

```c
#include "saferead.h"

// Após scanner_scan_all(), use SafeRead para leituras protegidas:
uint8_t buf[64 * 1024];
SafeReadResult r = saferead_block(&scan, block_idx, buf, sizeof(buf));

switch (r.status) {
    case SR_OK:      /* leitura ok, r.bytes_read bytes em buf */ break;
    case SR_SKIPPED: /* bloco BAD conhecido — pulado sem I/O  */ break;
    case SR_ERROR:   /* erro inesperado em bloco não-BAD      */ break;
}
```

---

## Build

```bash
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
export PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin

make        # gera PS3SHH.elf + PS3SHH.self
make pkg    # gera PS3SHH.pkg para instalar no PS3
```

---

## Implementação do framebuffer

`renderer.c` usa **software rasterizer** escrevendo direto na VRAM:

```c
// Alocação (renderer_init)
fb_ptr[i] = rsxMemalign(64, PITCH * SCREEN_H);
rsxAddressToOffset(fb_ptr[i], &fb_offset[i]);
gcmSetDisplayBuffer(i, fb_offset[i], PITCH, SCREEN_W, SCREEN_H);

// Pixel write (draw_rect)
fb_ptr[cur_fb][row * SCREEN_W + col] = 0xFFRRGGBB;

// Apresentar (renderer_flip)
gcmSetFlip(ctx, cur_fb);
rsxFlushBuffer(ctx);
gcmSetWaitFlip(ctx);
cur_fb ^= 1;
```

Para performance máxima, substitua o loop por `gcmSetTransferImage` (DMA).
O software path é suficiente para 256 células + UI a 60fps no PPU.

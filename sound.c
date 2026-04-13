#include "sound.h"
#include <audio/audio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE  48000
#define CHANNELS     2          /* stereo */

static uint32_t port_num = 0;
static int      audio_ok = 0;

static audioPortConfig port_cfg;
static audioPortParam  port_param;

/* ── Gera e toca um tom senoidal com envelope suave ────────────────────── */
static void play_tone(float freq_hz, uint32_t duration_ms, float volume) {
    if (!audio_ok) return;

    uint32_t n_samples = (uint32_t)((float)SAMPLE_RATE
                                    * (float)duration_ms / 1000.0f);
    uint32_t buf_size  = n_samples * CHANNELS * sizeof(float);

    float *buf = (float *)malloc(buf_size);
    if (!buf) return;

    for (uint32_t i = 0; i < n_samples; i++) {
        /* Envelope: fade-in 10%, fade-out 20% — elimina clique de áudio */
        float env = 1.0f;
        uint32_t fade_in  = n_samples / 10;
        uint32_t fade_out = n_samples * 8 / 10;

        if (i < fade_in)
            env = (float)i / (float)fade_in;
        else if (i > fade_out)
            env = (float)(n_samples - i) / (float)(n_samples - fade_out);

        float s = sinf(2.0f * 3.14159265f * freq_hz
                       * (float)i / (float)SAMPLE_RATE)
                  * volume * env;

        buf[i * CHANNELS]     = s;   /* L */
        buf[i * CHANNELS + 1] = s;   /* R */
    }

    /* Escreve no ring buffer da porta de áudio */
    uint64_t port_addr = 0;
    audioGetPortBlockTag(port_num, 0, &port_addr);

    void *ring = (void *)(uintptr_t)port_addr;
    memcpy(ring, buf, buf_size < port_cfg.size ? buf_size : port_cfg.size);

    free(buf);
    audioPortStart(port_num);
}

/* ── API pública ────────────────────────────────────────────────────────── */

void sound_init(void) {
    if (audioInit() != 0) return;

    port_param.numChannels = AUDIO_PORT_2CH;
    port_param.numBlocks   = AUDIO_BLOCK_8;
    port_param.attr        = 0;
    port_param.level       = 1.0f;

    if (audioPortOpen(&port_param, &port_num) != 0) return;
    if (audioGetPortConfig(port_num, &port_cfg)  != 0) return;

    audio_ok = 1;
}

void sound_beep_bad(void) {
    /* 880 Hz (Lá5) — 60ms — curto e assertivo como radar */
    play_tone(880.0f, 60, 0.35f);
}

void sound_beep_done(void) {
    /* Tom duplo ascendente: 660 Hz + 880 Hz */
    play_tone(660.0f, 100, 0.25f);
    play_tone(880.0f, 120, 0.30f);
}

void sound_shutdown(void) {
    if (!audio_ok) return;
    audioPortStop(port_num);
    audioPortClose(port_num);
    audioQuit();
    audio_ok = 0;
}

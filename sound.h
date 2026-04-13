#ifndef SOUND_H
#define SOUND_H

/* Sons de feedback do PS3SHH
 * Implementado via libaudio (PSL1GHT)
 * Gera tons senoidais puros em tempo real — sem assets externos
 */
void sound_init    (void);
void sound_beep_bad(void);   /* 880 Hz curto — bloco BAD encontrado */
void sound_beep_done(void);  /* tom duplo ascendente — scan completo */
void sound_shutdown(void);

#endif

#ifndef AUDIO_UI_H
#define AUDIO_UI_H
#include <stdint.h>
#include "audio_protocol.h"
extern uint8_t audio_ui;
/* Retained until next event, so a busy CAN TX retries in later snapshots. */
static inline void audio_event(uint8_t event) {
 audio_ui = ((audio_ui + 64u) & 0xc0u) | (event << 3);
}
#endif

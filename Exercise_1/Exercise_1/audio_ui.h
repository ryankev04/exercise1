#ifndef AUDIO_UI_H
#define AUDIO_UI_H
#include <stdint.h>
#include "audio_protocol.h"
extern uint8_t audio_ui;
static inline void audio_event(uint8_t event) {
    audio_ui = ((audio_ui + AUD_SEQUENCE_STEP) & AUD_SEQUENCE_MASK) |
               (event << AUD_EFFECT_SHIFT);
}
/************************************************************************/
/*
 * Retained until next event, so a busy CAN TX retries in later snapshots.
 */
/************************************************************************/
#endif

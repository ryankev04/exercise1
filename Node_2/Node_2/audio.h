#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
#include "audio_protocol.h"
enum { FX_HIT=7, FX_LOW, FX_HIGH };
void audio_init(uint32_t seed);
void audio_snapshot(uint8_t packed);
void audio_goal(uint16_t goals);
void audio_effect(uint8_t effect);
/* Backend contract: nonblocking; stop old music before replacement; loop music.
 * Effects overlay music. Priority: lost/error > life > hit > menu.
 * Backend is sound.c; callbacks queue work for foreground playback. */
void audio_backend_music(const char *path);
void audio_backend_effect(const char *path, uint8_t priority);
#endif

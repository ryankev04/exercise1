#ifndef SOUND_MIX_H
#define SOUND_MIX_H
#include <stdint.h>
static inline uint16_t sound_mix_to_dac(int16_t music, int16_t effect, uint8_t fx_active, uint16_t gain) {
    /*
     * Preserve music level when alone; duck it while an effect overlays it.
     */
    int32_t value = fx_active ? (int32_t)music / 4 + (int32_t)effect : music;
    value = (value * (int32_t)gain) / 256;
    if (value > 32767) {
        value = 32767;
    }
    if (value < -32768) {
        value = -32768;
    }
    return (uint16_t)(2048 + value / 16);
}
#endif

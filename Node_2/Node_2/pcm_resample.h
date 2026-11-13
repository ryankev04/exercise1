#ifndef PCM_RESAMPLE_H
#define PCM_RESAMPLE_H
#include <stdint.h>
#define PCM_OUTPUT_RATE 48000u
typedef struct {
    uint32_t phase;
    int16_t previous;
    uint8_t have;
} pcm_resample_t;
static inline uint8_t pcm_resample_frame(pcm_resample_t *resampler, int16_t current, uint32_t rate,
                                         int16_t output[12]) {
    uint8_t sample_count = 0;
    if (rate == PCM_OUTPUT_RATE) {
        output[0] = current;
        resampler->previous = current;
        resampler->have = 1;
        resampler->phase = 0;
        return 1;
    }
    if (!resampler->have) {
        resampler->previous = current;
        resampler->have = 1;
    }
    while (resampler->phase < PCM_OUTPUT_RATE) {
        int32_t delta = (int32_t)current - resampler->previous;
        /*
         * |delta|<=65535 and phase<48000: product<=3,145,614,465,
         * which fits uint32. Preserve signed truncation exactly, without a
         * software64-bit division on the SAM3X for every output sample.
         */
        uint32_t magnitude = (uint32_t)(delta < 0 ? -delta : delta);
        uint32_t step = (magnitude * resampler->phase) / PCM_OUTPUT_RATE;
        output[sample_count++] = (int16_t)((int32_t)resampler->previous + (delta < 0 ? -(int32_t)step : (int32_t)step));
        resampler->phase += rate;
    }
    resampler->phase -= PCM_OUTPUT_RATE;
    resampler->previous = current;
    return sample_count;
}
/************************************************************************/
/*
 * Linear interpolation to a fixed clock permits different music/SFX rates.
 * rate must be validated 4000..48000. Returns <=12 samples per native frame.
 */
/************************************************************************/
#endif

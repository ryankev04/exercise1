#ifndef PCM_RESAMPLE_H
#define PCM_RESAMPLE_H
#include <stdint.h>
#define PCM_OUTPUT_RATE 48000u
typedef struct {uint32_t phase;int16_t previous;uint8_t have;} pcm_resample_t;
/* Linear interpolation to a fixed clock permits different music/SFX rates.
 * rate must be validated 4000..48000. Returns <=12 samples per native frame. */
static inline uint8_t pcm_resample_frame(pcm_resample_t *r,int16_t current,uint32_t rate,int16_t output[12]) {
 uint8_t n=0;
 if(rate==PCM_OUTPUT_RATE){output[0]=current;r->previous=current;r->have=1;r->phase=0;return 1;}
 if(!r->have){r->previous=current;r->have=1;}
 while(r->phase<PCM_OUTPUT_RATE) {
  int32_t delta=(int32_t)current-r->previous;
  /* |delta|<=65535 and phase<48000: product<=3,145,614,465,
   * which fits uint32. Preserve signed truncation exactly, without a
   * software64-bit division on the SAM3X for every output sample. */
  uint32_t magnitude=(uint32_t)(delta<0 ? -delta : delta);
  uint32_t step=(magnitude*r->phase)/PCM_OUTPUT_RATE;
  output[n++]=(int16_t)((int32_t)r->previous+(delta<0 ? -(int32_t)step : (int32_t)step));
  r->phase+=rate;
 }
 r->phase-=PCM_OUTPUT_RATE;r->previous=current;return n;
}
#endif

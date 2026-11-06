#include "audio.h"
#include <stddef.h>
static uint8_t state, token, seen;
static uint16_t previous_goals;
static uint32_t random_state;
static uint8_t choose(uint8_t count) {
 random_state ^= random_state << 13;
 random_state ^= random_state >> 17;
 random_state ^= random_state << 5;
 return (uint8_t)(random_state % count);
}
static const char *const kubbi[] = {
 "MUSIC/kubbi_firelight.wav", "MUSIC/kubbi_something.wav"
};
static const char *const pablo[] = {
 "MUSIC/pablo_stage1.wav", "MUSIC/pablo_stage2.wav", "MUSIC/pablo_stage3.wav"
};
static const char *const effects[] = {
 NULL, "SFX/q3_navigate.wav", "SFX/q3_enter.wav", "SFX/q3_back.wav",
 "SFX/q3_change.wav", "SFX/q3_error.wav", "SFX/rocketrace_lost.wav",
 "SFX/ball_hit.wav", "SFX/rocketrace_low_rise.wav", "SFX/rocketrace_high_rise.wav"
};
void audio_init(uint32_t seed) {
 state=255; token=0; seen=0; previous_goals=0;
 random_state=seed ? seed : 1;
}
void audio_effect(uint8_t effect) {
 if (!effect || effect >= sizeof effects / sizeof effects[0]) return;
 audio_backend_effect(effects[effect],
  effect==AUD_LOST || effect==AUD_ERROR ? 3 : effect>=FX_LOW ? 2 : effect==FX_HIT ? 1 : 0);
}
void audio_snapshot(uint8_t packed) {
 uint8_t next=packed & 7u, next_token=packed & 0xf8u;
 if (next>AUD_OVER) return;
 if (next!=state) {
  const char *path=NULL;
  switch(next) {
   case AUD_MENU: path=kubbi[choose(2)]; break;
   case AUD_BLUE: path=pablo[choose(3)]; break;
   case AUD_BLUE_PAUSE: path="MUSIC/pablo_hills.wav"; break;
   case AUD_TURBO: path="MUSIC/mario_theme.wav"; break;
   case AUD_TURBO_PAUSE: path="MUSIC/pablo_city.wav"; break;
   case AUD_OVER: path = "MUSIC/pablo_lounge.wav"; break; 
   default: break; /* Game over: stop soundtrack; lost effect can finish. */
  }
  if (next==AUD_MENU) previous_goals=0;
  state=next;
  audio_backend_music(path);
 }
 /* First packet establishes baseline; don't replay an old retained effect
  * after Node 2 reboots. Later repeated snapshots never retrigger effects. */
 if (seen && next_token!=token && ((packed>>3)&7u)<=AUD_LOST) audio_effect((packed>>3)&7u);
 token=next_token; seen=1;
}
void audio_goal(uint16_t goals) {
 if (goals>previous_goals && state==AUD_BLUE) {
  /* Last life overrides low-rise on the same loss: 3 -> 2 low,
   * 2 -> 1 high, 1 -> 0 lost. Authoritative IR goal count. */
  audio_effect(goals>=3 ? AUD_LOST : goals==2 ? FX_HIGH : FX_LOW);
 }
 if (!goals || state==AUD_BLUE) previous_goals=goals;
}

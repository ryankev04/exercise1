#include "audio.h"
#include <stddef.h>

static uint8_t soundtrack_state;
static uint8_t last_event_token;
static uint8_t have_snapshot;
static uint16_t previous_goals;
static uint32_t random_state;
static uint8_t choose_track(uint8_t count) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (uint8_t)(random_state % count);
}

static const char *const menu_tracks[] = {
    "MUSIC/kubbi_firelight.wav",
    "MUSIC/kubbi_something.wav",
};

static const char *const ping_pong_tracks[] = {
    "MUSIC/pablo_stage1.wav",
    "MUSIC/pablo_stage2.wav",
    "MUSIC/pablo_stage3.wav",
};

static const char *const effect_paths[] = {
    [0]          = NULL,
    [AUD_NAV]    = "SFX/q3_navigate.wav",
    [AUD_ENTER]  = "SFX/q3_enter.wav",
    [AUD_BACK]   = "SFX/q3_back.wav",
    [AUD_CHANGE] = "SFX/q3_change.wav",
    [AUD_ERROR]  = "SFX/q3_error.wav",
    [AUD_LOST]   = "SFX/rocketrace_lost.wav",
    [FX_HIT]     = "SFX/ball_hit.wav",
    [FX_LOW]     = "SFX/rocketrace_low_rise.wav",
    [FX_HIGH]    = "SFX/rocketrace_high_rise.wav",
};

void audio_init(uint32_t seed) {
    soundtrack_state = 255;
    last_event_token = 0;
    have_snapshot = 0;
    previous_goals = 0;
    random_state = seed ? seed : 1;
}

void audio_effect(uint8_t effect) {
    if (!effect || effect >= sizeof effect_paths / sizeof effect_paths[0]) {
        return;
    }
    audio_backend_effect(effect_paths[effect],
                         effect == AUD_LOST || effect == AUD_ERROR ? 3 :
                         effect >= FX_LOW ? 2 :
                         effect == FX_HIT ? 1 : 0);
}

void audio_snapshot(uint8_t snapshot) {
    uint8_t next_state = snapshot & AUD_STATE_MASK;
    uint8_t event_token = snapshot & AUD_EVENT_TOKEN_MASK;
    if (next_state > AUD_OVER) {
        return;
    }
    if (next_state != soundtrack_state) {
        const char *path = NULL;
        switch (next_state) {
        case AUD_MENU:
            path = menu_tracks[choose_track(2)];
            break;
        case AUD_BLUE:
            path = ping_pong_tracks[choose_track(3)];
            break;
        case AUD_BLUE_PAUSE:
            path = "MUSIC/pablo_hills.wav";
            break;
        case AUD_TURBO:
            path = "MUSIC/mario_theme.wav";
            break;
        case AUD_TURBO_PAUSE:
            path = "MUSIC/pablo_city.wav";
            break;
        default:
            break; // Game over: stop soundtrack; lost effect can finish.
        }
        if (next_state == AUD_MENU) {
            previous_goals = 0;
        }
        soundtrack_state = next_state;
        audio_backend_music(path);
    }
    /*
     * First packet establishes baseline; don't replay an old retained effect
     * after Node 2 reboots. Later repeated snapshots never retrigger effects.
     */
    if (have_snapshot && event_token != last_event_token &&
        ((snapshot >> AUD_EFFECT_SHIFT) & AUD_EFFECT_MASK) <= AUD_LOST) {
        audio_effect((snapshot >> AUD_EFFECT_SHIFT) & AUD_EFFECT_MASK);
    }
    last_event_token = event_token;
    have_snapshot = 1;
}

void audio_goal(uint16_t goals) {
    if (goals > previous_goals && soundtrack_state == AUD_BLUE) {
        /*
         * Last life overrides low-rise on the same loss: 3 -> 2 low,
         * 2 -> 1 high, 1 -> 0 lost. Authoritative IR goal count.
         */
        audio_effect(goals >= 3 ? AUD_LOST : goals == 2 ? FX_HIGH : FX_LOW);
    }
    if (!goals || soundtrack_state == AUD_BLUE) {
        previous_goals = goals;
    }
}

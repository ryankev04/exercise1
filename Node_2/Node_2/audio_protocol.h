#ifndef AUDIO_PROTOCOL_H
#define AUDIO_PROTOCOL_H
/* Joystick 0x100 byte 7: state[2:0], UI effect[5:3], sequence[7:6].
 * Bytes 0..6 and physical game commands are unchanged. */
#define AUD_STATE_MASK       0x07u
#define AUD_EFFECT_SHIFT     3
#define AUD_EFFECT_MASK      0x07u
#define AUD_EVENT_TOKEN_MASK 0xf8u
#define AUD_SEQUENCE_MASK    0xc0u
#define AUD_SEQUENCE_STEP    64u

#define AUD_MENU 0
#define AUD_BLUE 1
#define AUD_BLUE_PAUSE 2
#define AUD_TURBO 3
#define AUD_TURBO_PAUSE 4
#define AUD_OVER 5

#define AUD_NAV 1
#define AUD_ENTER 2
#define AUD_BACK 3
#define AUD_CHANGE 4
#define AUD_ERROR 5
#define AUD_LOST 6
#endif

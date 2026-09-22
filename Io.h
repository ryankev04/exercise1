/*
 * Io.h
 *
 * Created: 22.09.2026 14:50:16
 *  Author: ngabo
 */ 
#ifndef IO_H_
#define IO_H_

#include <stdint.h>

// Valid input bits returned by the User-I/O board firmware.
#define IO_RIGHT_BUTTON_MASK 0x3F
#define IO_LEFT_BUTTON_MASK  0x7F
#define IO_NAV_BUTTON_MASK   0x1F

typedef struct {
	uint8_t x;
	uint8_t y;
	uint8_t btn;
} io_joystick_t;

typedef struct {
	uint8_t right;   // [0 0 R6 R5 R4 R3 R2 R1]
	uint8_t left;     // [0 L7 L6 L5 L4 L3 L2 L1]
	uint8_t nav;      // [0 0 0 Up Down Left Right Btn]
} io_buttons_t;

io_buttons_t io_read_buttons(void);
void io_led_set(uint8_t led_n, uint8_t on);
void io_led_groups_update(uint8_t left_active, uint8_t right_active);
io_joystick_t io_read_joystick(void);

#endif //IO_H_
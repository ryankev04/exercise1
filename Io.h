/*
 * Io.h
 *
 * Created: 22.09.2026 14:50:16
 *  Author: ngabo
 */ 
#ifndef IO_H_
#define IO_H_

#include <stdint.h>

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
io_joystick_t io_read_joystick(void);

#endif //IO_H_
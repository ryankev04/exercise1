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

io_joystick_t io_read_joystick(void);

#endif //IO_H_
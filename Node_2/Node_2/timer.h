/*
 * timer.h
 *
 * Created: 13.10.2026 14:50:07
 *  Author: ngabo
 * Used to read and filter the IR sensor at a fixed rate (1000 times per
 * second), even while printf() is busy in the main loop.
 */ 

#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>

/* Start the 1 ms interrupt */
void timer_init(void);

/* Milliseconds since timer_init() */
uint32_t timer_millis(void);

#endif /* TIMER_H_ */
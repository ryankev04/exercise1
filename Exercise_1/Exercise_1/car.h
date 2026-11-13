/*
 * car.h - TURBO RUSH, a side-scrolling highway game on the OLED (Node 1)
 *
 *   Drive in one of 3 lanes and dodge the traffic coming towards you.
 *   Joystick up/down = change lane, hold SR5 = boost (faster, more points),
 *   coins give +50 points. The game gets faster the longer you survive.
 *
 * The game runs one frame per main-loop pass: call car_update() every pass.
 */
#ifndef CAR_H_
#define CAR_H_

#include <stdint.h>

void     car_start(uint16_t seed);                    // new game (seed = any number, not 0)
uint8_t  car_update(int8_t lane_move, uint8_t boost); // one frame; returns 1 = crashed
void     car_redraw(void);                            // draw everything again (after a pause)
void     car_crash_animation(void);                   // flashes + "CRASH!"
uint16_t car_score(void);

#endif /* CAR_H_ */

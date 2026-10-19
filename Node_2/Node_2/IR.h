/*
 * IR.h
 *
 * Created: 13.10.2026 14:50:37
 *  Author: ngabo
 *
 * Every 1 ms (from the timer interrupt):
 *   1. read the ADC
 *   2. low-pass filter it (digital filter)
 *   3. decide "beam blocked" or "beam clear" using two thresholds
 *   4. only accept a change if it lasts IR_DEBOUNCE_MS
 *   5. count a goal when the beam goes from clear to blocked,
 *      but not again within IR_GOAL_HOLDOFF_MS (ball bouncing)
 */

#ifndef IR_H_
#define IR_H_
 
#include <stdint.h>
 

 
#endif /* IR_H_ */
 
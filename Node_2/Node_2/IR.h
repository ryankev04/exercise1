/*
 * IR.h
 *
 * Created: 13.10.2026 14:50:37
 *  Author: ngabo
 *
 * The timer interrupt samples AD7 and uses two thresholds to update
 * the beam state. Goal detection requires consecutive blocked samples
 * and applies a hold-off after a goal.
 */

#ifndef IR_H_
#define IR_H_
 
#include <stdint.h>

uint8_t IR_is_blocked(void);
void IR_update(void);
uint8_t IR_goal_happened(void);
uint16_t IR_get_score(void);
void IR_test(void);
 
#endif /* IR_H_ */
 
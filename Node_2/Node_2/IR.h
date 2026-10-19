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
 
/* ---- Settings: tune these with the printout ---------------------------
 * ADC values are 0..4095 (0..3.3 V).
 * 1. Run the program, look at "filt" with the beam CLEAR and BLOCKED.
 * 2. Put IR_TH_BLOCK about 60 % and IR_TH_CLEAR about 40 % of the way
 *    from the clear value to the blocked value.                         */
#define IR_BLOCKED_IS_HIGH   1      /* 1: value goes UP when blocked
                                       0: value goes DOWN when blocked   */
#define IR_TH_BLOCK          2500   /* cross this -> blocked  (placeholder!) */
#define IR_TH_CLEAR          1500   /* cross back -> clear    (placeholder!) */
 
#define IR_DEBOUNCE_MS       15     /* state must hold this long           */
#define IR_GOAL_HOLDOFF_MS   1500   /* no new goal within this time        */
 
/* Call once at startup */
void ir_init(void);
 
/* Called every 1 ms from the timer interrupt (timer.c) */
void ir_update(void);
 
/* For printing / the game */
uint16_t ir_get_raw(void);          /* last ADC value                     */
uint16_t ir_get_filtered(void);     /* after the low-pass filter          */
uint8_t  ir_is_blocked(void);       /* 1 = beam blocked (debounced)       */
uint32_t ir_get_score(void);        /* number of goals                    */
void     ir_reset_score(void);
uint8_t  ir_goal_happened(void);    /* 1 once for each new goal, then 0   */
 
#endif /* IR_H_ */
 
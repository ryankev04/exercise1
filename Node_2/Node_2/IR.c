/*
 * IR.c
 *
 * Created: 13.10.2026 14:50:20
 *  Author: ngabo
 */
#include "sam.h"
#include "IR.h"
#include "adc.h"
#include <stdio.h>
 
 
#define IR_TH_BLOCK  800  // below this -> beam is blocked
#define IR_TH_CLEAR  1600 // above this -> beam is clear again

static volatile uint8_t  blocked  = 0; // current state (changed in the interrupt)
static volatile uint16_t score    = 0; // number of goals
static volatile uint8_t  new_goal = 0; // Latches an unconsumed goal indication.
static uint8_t was_blocked = 0;        // Last blocked state assigned by IR_update().

#define IR_DEBOUNCE_MS      5    // Consecutive blocked updates required for a goal.
#define IR_GOAL_HOLDOFF_MS  2000 // Timer updates between eligible goals.

static uint8_t  blocked_ms = 0; // how long the beam has been blocked
static uint16_t holdoff_ms = 0; // > 0: goals are ignored for this many more ms

uint8_t IR_is_blocked(void)
{
    uint16_t value = adc_read(7);

    if (!blocked && value < IR_TH_BLOCK) {
        blocked = 1;
    }
    else if (blocked && value > IR_TH_CLEAR) {
        blocked = 0;
    }
    return blocked;
}
/************************************************************************/
/*
 * IR_is_blocked(): Samples ADC channel 7 and updates the cached beam
 * state. A reading below IR_TH_BLOCK marks it blocked. A reading above
 * IR_TH_CLEAR marks it clear. Between the thresholds, including the
 * endpoints, the previous state is retained.
 */
/************************************************************************/

void IR_update(void)
{
    uint8_t now_blocked = IR_is_blocked();

    if (holdoff_ms > 0) {
        holdoff_ms--;
    }

    // Count how many ms in a row the beam has been blocked (max 255)
    if (now_blocked) {
        if (blocked_ms < 255) blocked_ms++;
    } else {
        blocked_ms = 0;
    }

    // Goal = blocked for exactly IR_DEBOUNCE_MS (counted once per block),
    //        and not within the hold-off time of the last goal
    if (blocked_ms == IR_DEBOUNCE_MS && holdoff_ms == 0) {
        score++;
        new_goal = 1;
        holdoff_ms = IR_GOAL_HOLDOFF_MS;
    }
    was_blocked = now_blocked;
}
/************************************************************************/
/*
 * IR_update(): Runs in the 1 ms timer interrupt. Counts consecutive
 * blocked samples and records a goal on the fifth sample if the
 * hold-off has expired. The goal increments the score, latches a
 * pending event, and starts a new hold-off. A blockage that reaches
 * five samples during hold-off cannot score later in that same block.
 */
/************************************************************************/

uint8_t IR_goal_happened(void)
{
    if (new_goal == 1) {
        new_goal = 0;
        return 1;
    }
    return 0;
}
/************************************************************************/
/*
 * IR_goal_happened(): Returns 1 and clears the pending goal flag if it
 * was set. The flag indicates an event and does not accumulate a count.
 * Returns 0 when no goal is pending.
 */
/************************************************************************/

uint16_t IR_get_score(void)
{
    return score;
}
/************************************************************************/
/*
 * IR_get_score(): Returns the accumulated goal count.
 */
/************************************************************************/

void IR_test(void)
{
    uint16_t value = adc_read(7);
    uint32_t mv = (uint32_t)value * 3300 / 4095;

    if (blocked) {
        printf("IR value = %u  (%lu mV)  BLOCKED\r\n", value, (unsigned long)mv);
    } else {
        printf("IR value = %u  (%lu mV)  clear\r\n", value, (unsigned long)mv);
    }
}
/************************************************************************/
/*
 * IR_test(): Prints the raw ADC reading, a millivolt estimate scaled to
 * 3300 mV, and the cached beam state. This diagnostic leaves the beam
 * state unchanged.
 */
/************************************************************************/


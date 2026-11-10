/*
 * IR.c
 *
 * Created: 13.10.2026 14:50:20
 *  Author: ngabo
 * See ir.h for what each step does and how to tune the settings.
 */
#include "sam.h"
#include "IR.h"
#include "adc.h"
#include <stdio.h>
 
 
#define IR_TH_BLOCK  800    // below this -> beam is blocked
#define IR_TH_CLEAR  1600   // above this -> beam is clear again

static volatile uint8_t  blocked  = 0;   // current state (changed in the interrupt)
static volatile uint16_t score    = 0;   // number of goals
static volatile uint8_t  new_goal = 0;   // 1 = goal happened, main has not printed it yet
static uint8_t was_blocked = 0;          // state from the previous millisecond

/* Reads the ADC and updates the state  */
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

/* Called from TC0_Handler every 1 ms. NO printf here! */
void IR_update(void)
{
    uint8_t now_blocked = IR_is_blocked();

    if (now_blocked == 1 && was_blocked == 0) {   // clear -> BLOCKED = goal
        score++;
        new_goal = 1;
    }
    was_blocked = now_blocked;
}

/* Called from main: returns 1 once per goal */
uint8_t IR_goal_happened(void)
{
    if (new_goal == 1) {
        new_goal = 0;
        return 1;
    }
    return 0;
}

uint16_t IR_get_score(void)
{
    return score;
}

/* For testing from main: prints the value and the state.
 * Only READS 'blocked', the interrupt is the one that changes it. */
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


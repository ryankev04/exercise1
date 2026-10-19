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
 
/* "volatile" because these are changed in the interrupt and read in main */
static volatile uint16_t raw          = 0;
static volatile uint16_t filtered     = 0;
static volatile uint8_t  blocked      = 0;
static volatile uint16_t stable_count = 0;
static volatile uint32_t time_since_goal = IR_GOAL_HOLDOFF_MS;
static volatile uint32_t score        = 0;
static volatile uint8_t  new_goal     = 0;
 
void ir_init(void)
{
    adc_init();
 
    /* Give the ADC a moment to start, then use the first reading as the
     * filter's starting value (otherwise the filter would start at 0). */
    for (volatile uint32_t i = 0; i < 100000; i++) { }
    raw      = adc_read(ADC_IR_CHANNEL);
    filtered = raw;
}
 
/* Helper: does this value look like "blocked" / "clear"? */
static uint8_t looks_blocked(uint16_t v)
{
#if IR_BLOCKED_IS_HIGH
    return v > IR_TH_BLOCK;
#else
    return v < IR_TH_BLOCK;
#endif
}
 
static uint8_t looks_clear(uint16_t v)
{
#if IR_BLOCKED_IS_HIGH
    return v < IR_TH_CLEAR;
#else
    return v > IR_TH_CLEAR;
#endif
}
 
/* Runs every 1 ms (called from TC0_Handler in timer.c) */
void ir_update(void)
{
    /* ---- Step 1: read the ADC ---------------------------------------- */
    raw = adc_read(ADC_IR_CHANNEL);
 
    /* ---- Step 2: digital low-pass filter -------------------------------
     * new = old + (input - old) / 8
     * Each sample moves the result 1/8 of the way to the new value, so a
     * single noisy sample has little effect. Integer maths, so we work with
     * signed numbers to handle input < old.                              */
    int32_t diff = (int32_t)raw - (int32_t)filtered;
    filtered = (uint16_t)((int32_t)filtered + diff / 8);
 
    /* ---- Step 3 + 4: thresholds (hysteresis) and debounce --------------
     * Two thresholds: to become "blocked" the value must pass IR_TH_BLOCK,
     * to become "clear" again it must pass IR_TH_CLEAR. Values in between
     * keep the current state, so noise near one threshold can't flicker.
     * And the new state must hold for IR_DEBOUNCE_MS in a row.           */
    uint8_t wants_change;
    if (blocked) wants_change = looks_clear(filtered);
    else         wants_change = looks_blocked(filtered);
 
    if (time_since_goal < 0xFFFFFFFF) time_since_goal++;
 
    if (wants_change) {
        stable_count++;
        if (stable_count >= IR_DEBOUNCE_MS) {
            blocked = !blocked;          /* accept the new state */
            stable_count = 0;
 
            /* ---- Step 5: goal = beam goes from clear to blocked ------- */
            if (blocked && time_since_goal >= IR_GOAL_HOLDOFF_MS) {
                score++;
                new_goal = 1;
                time_since_goal = 0;
            }
        }
    } else {
        stable_count = 0;                /* state did not hold, start over */
    }
}
 
uint16_t ir_get_raw(void)      { return raw; }
uint16_t ir_get_filtered(void) { return filtered; }
uint8_t  ir_is_blocked(void)   { return blocked; }
uint32_t ir_get_score(void)    { return score; }
 
void ir_reset_score(void)
{
    __disable_irq();                     /* don't let the interrupt change */
    score = 0;                           /* these while we reset them      */
    new_goal = 0;
    time_since_goal = IR_GOAL_HOLDOFF_MS;
    __enable_irq();
}
 
uint8_t ir_goal_happened(void)
{
    __disable_irq();
    uint8_t g = new_goal;
    new_goal = 0;
    __enable_irq();
    return g;
}
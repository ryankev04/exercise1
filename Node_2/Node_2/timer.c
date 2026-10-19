/*
 * timer.c
 *
 * Created: 13.10.2026 14:49:57
 *  Author: ngabo
* 1 ms interrupt from Timer/Counter 0, channel 0 (Node 2)
 *
 * Datasheet references (SAM3X):
 *   TC0 peripheral ID 27 ............ section 9.1,       page 38
 *   PMC_PCER0 ....................... section 28.15.4,   page 542
 *   TIMER_CLOCK1 = MCK/2 ............ Table 36-1,        page 857
 *   WAVSEL = 10 (count up to RC) .... section 36.6.11.2, page 868
 *   TC_CCR .......................... section 36.7.1,    page 880
 *   TC_CMR (waveform mode) .......... section 36.7.3,    page 883
 *   TC_RC ........................... section 36.7.8,    page 891
 *   TC_SR (read clears flags) ....... section 36.7.9,    page 892
 *   TC_IER .......................... section 36.7.10,   page 894
 *   NVIC (enable interrupt) ......... section 10.20,     page 152
 *
 * Calculation:
 *   TIMER_CLOCK1 = MCK / 2 = 84 000 000 / 2 = 42 000 000 counts per second
 *   We want 1 interrupt every 1 ms = 0.001 s
 *   RC = 42 000 000 * 0.001 = 42 000
 */

#include "sam.h"
#include "timer.h"
#include "IR.h"
 
static volatile uint32_t millis = 0;
 
void timer_init(void)
{
    // Turn on the clock to TC0. ID 27 is below 32, so it goes in PCER0. 
    PMC->PMC_PCER0 = (1u << ID_TC0);
 
   // Stop the counter while configuring it 
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKDIS;
 
    /*  Mode:
     *      TIMER_CLOCK1 -> counts at MCK/2 = 42 MHz
     *      WAVE         -> waveform mode
     *      WAVSEL_UP_RC -> count 0, 1, 2 ... RC, then back to 0      */
    TC0->TC_CHANNEL[0].TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1| TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC;
 
    // Count to 42 000 -> one period = 1 ms 
    TC0->TC_CHANNEL[0].TC_RC = 42000;
 
    // Interrupt when the counter reaches RC 
    TC0->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;
 
    //. Allow the TC0 interrupt in the NVIC 
    NVIC_EnableIRQ(TC0_IRQn);
 
    // Start: enable the clock and reset the counter to 0 
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}
 
uint32_t timer_millis(void)
{
    return millis;
}
 
/* This runs automatically every 1 ms */
void TC0_Handler(void)
{
    /* Reading the status register clears the interrupt flag.
     * If we did not read it, the interrupt would fire again immediately. */
    uint32_t status = TC0->TC_CHANNEL[0].TC_SR;
 
    if (status & TC_SR_CPCS) {
        millis++;
        ir_update();          /* read and filter the IR sensor */
    }
}


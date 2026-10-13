/*
 * pwm.c
 *
 * Created: 13.10.2026 12:15:53
 *  Author: ngabo
 * 
 *  Simple servo driver for Node 2 
 *
 * Datasheet references (SAM3X):
 *   PWM peripheral ID 36 ............ section 9.1,      page 38
 *   PB13 = PWMH1, peripheral B ...... section 9.3.2,    page 41
 *   PMC_PCER1 ....................... section 28.15.23, page 563
 *   PWM_ENA / PWM_DIS ............... pages 1007, 1008
 *   PWM_CMR (CPRE, CPOL) ............ page 1044
 *   PWM_CDTY / PWM_CDTYUPD .......... pages 1046, 1047
 *   PWM_CPRD ........................ page 1048
 *   PIO_PDR / PIO_ABSR .............. pages 634, 656
 *   Initialization order ............ section 38.6.5.1, page 996
 */
#include "sam.h"
#include "pwm.h"
 
// Joystick X from Node 1 (raw byte from the User-I/O board).

#define JOY_MIN      0
#define JOY_CENTER   126      /* same as NAV_CENTER in Node 1's Menu.c */
#define JOY_MAX      255
 
static uint32_t current_cdty = SERVO_CDTY_CENTER;
 
/* ------------------------------------------------------------------ */
/* Step 1: set up the PWM                                             */
/* ------------------------------------------------------------------ */
void servo_init(void)
{
    /*  Turn on the clock to the PWM peripheral.
     *    PWM has ID 36. IDs 32..63 go in PCER1, so the bit is 36 - 32 = 4. */
    PMC->PMC_PCER1 = (1u << (ID_PWM - 32));
 
    //  Turn channel 1 off while we configure it, and wait until it is off. 
    PWM->PWM_DIS = PWM_DIS_CHID1;
    while (PWM->PWM_SR & PWM_SR_CHID1) { }
 
    /* Channel mode:
     *      CPRE = MCK/32  -> 84 MHz / 32 = 2 625 000 counts per second
     *      CPOL = 1       -> output starts HIGH, so CDTY = high time
     *      CALG = 0       -> left aligned (default, so not written)      */
    PWM->PWM_CH_NUM[1].PWM_CMR = PWM_CMR_CPRE_MCK_DIV_32 | PWM_CMR_CPOL;
 
    // Period: 52 500 counts = 20 ms 
    PWM->PWM_CH_NUM[1].PWM_CPRD = SERVO_CPRD;
 
    //  First pulse: 3937 counts = 1.5 ms = centre 
    PWM->PWM_CH_NUM[1].PWM_CDTY = SERVO_CDTY_CENTER;
    current_cdty = SERVO_CDTY_CENTER;
 
    // 6. Turn channel 1 on 
    PWM->PWM_ENA = PWM_ENA_CHID1;
 
    /*  Connect pin PB13 to the PWM (done last, so the servo only ever
     *    sees a fully configured signal).
     *      PIO_ABSR bit = 1 -> peripheral B (PWMH1 is on peripheral B)
     *      PIO_PDR        -> take the pin away from normal GPIO           */
    PIOB->PIO_ABSR |= PIO_PB13;
    PIOB->PIO_PDR   = PIO_PB13;
}
 

// Step 2: change the pulse, with safety limits                       

void servo_set_cdty(uint32_t cdty)
{
    // Safety: never outside 0.9 ms .. 2.1 ms 
    if (cdty < SERVO_CDTY_MIN) cdty = SERVO_CDTY_MIN;
    if (cdty > SERVO_CDTY_MAX) cdty = SERVO_CDTY_MAX;
 
    current_cdty = cdty;
 
    /* Write the UPDATE register, not PWM_CDTY. The new value is used from
     * the start of the next period, so a pulse is never cut short
     * (datasheet 38.6.5.3, page 997). */
    PWM->PWM_CH_NUM[1].PWM_CDTYUPD = cdty;
}
 
 

// Step 3: joystick X (0..255) -> CDTY (2363..5512) 

void servo_set_from_joystick(uint8_t x)
{
    int32_t cdty;
	
	
    if (x >= JOY_CENTER) {
        /* Right half: x = 126 .. 255  ->  cdty = 3937 .. 5512
         * Straight line through (126, 3937) and (255, 5512):
         *   cdty = 3937 + (x - 126) * (5512 - 3937) / (255 - 126)
         *        = 3937 + (x - 126) * 1575 / 129                     */
        cdty = SERVO_CDTY_CENTER  + ((int32_t)x - JOY_CENTER) * (SERVO_CDTY_MAX - SERVO_CDTY_CENTER)/ (JOY_MAX - JOY_CENTER);
    } else {
        /* Left half: x = 0 .. 126  ->  cdty = 2363 .. 3937
         *   cdty = 3937 - (126 - x) * (3937 - 2363) / (126 - 0)
         *        = 3937 - (126 - x) * 1574 / 126                     */
        cdty = SERVO_CDTY_CENTER - (JOY_CENTER - (int32_t)x) * (SERVO_CDTY_CENTER - SERVO_CDTY_MIN)  / (JOY_CENTER - JOY_MIN);
    }
 
    servo_set_cdty((uint32_t)cdty);   /* limits are applied in here */
}
 
uint32_t servo_get_cdty(void)
{
    return current_cdty;
}







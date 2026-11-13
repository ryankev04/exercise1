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
 
// Joystick input is a raw byte. Node 2 passes Y to the servo.

#define JOY_MIN      0 // Lower endpoint of the input mapping.
#define JOY_CENTER   126 // Input mapped to the centre pulse width.
#define JOY_MAX      255 // Upper endpoint of the input mapping.
 
static uint32_t current_cdty = SERVO_CDTY_CENTER; // Last clamped duty command.
 
void servo_init(void)
{
    /*
     * Turn on the clock to the PWM peripheral.
     *    PWM has ID 36. IDs 32..63 go in PCER1, so the bit is 36 - 32 = 4.
     */
    PMC->PMC_PCER1 = (1u << (ID_PWM - 32));
 
    //  Turn channel 1 off while we configure it, and wait until it is off.
    PWM->PWM_DIS = PWM_DIS_CHID1;
    while (PWM->PWM_SR & PWM_SR_CHID1) { }
 
    /*
     * Channel mode:
     *      CPRE = MCK/32  -> 84 MHz / 32 = 2 625 000 counts per second
     *      CPOL = 1       -> output starts HIGH, so CDTY = high time
     *      CALG = 0       -> left aligned (clear in this register write)
     */
    PWM->PWM_CH_NUM[1].PWM_CMR = PWM_CMR_CPRE_MCK_DIV_32 | PWM_CMR_CPOL;
 
    // Period: 52 500 counts = 20 ms
    PWM->PWM_CH_NUM[1].PWM_CPRD = SERVO_CPRD;
 
    // Initial duty setting: 3937 counts, approximately 1.5 ms.
    PWM->PWM_CH_NUM[1].PWM_CDTY = SERVO_CDTY_CENTER;
    current_cdty = SERVO_CDTY_CENTER;
 
    /*
     * Select PWMH1 on PB13. servo_enable(0) then leaves PB13 low.
     *      PIO_ABSR bit = 1 -> peripheral B (PWMH1 is on peripheral B)
     *      PIO_PDR        -> take the pin away from normal GPIO
     */
    PIOB->PIO_ABSR |= PIO_PB13;
    PIOB->PIO_PDR   = PIO_PB13;
	servo_enable(0);
}
/************************************************************************/
/*
 * servo_init(): Configures PWM channel 1 for servo pulses on PB13.
 * At 84 MHz MCK with a divide-by-32 clock, 52500 counts give a
 * 20 ms period. Loads the centre pulse width and leaves the output
 * disabled until servo_enable() is called.
 */
/************************************************************************/

void servo_enable(uint8_t enabled)
{
    if (enabled) {
        /*
         * Return PB13 to the PWM peripheral.
         */
        PIOB->PIO_ABSR |= PIO_PB13;
        PIOB->PIO_PDR = PIO_PB13;

        PWM->PWM_ENA = PWM_ENA_CHID1;
    }
    else {
        PWM->PWM_DIS = PWM_DIS_CHID1;

        /*
         * Drive the signal LOW as GPIO.
         * This removes the servo control pulses.
         */
        PIOB->PIO_CODR = PIO_PB13;
        PIOB->PIO_OER = PIO_PB13;
        PIOB->PIO_PER = PIO_PB13;
    }
}
/************************************************************************/
/*
 * servo_enable(): Connects PB13 to PWMH1 and enables channel 1 for
 * a nonzero argument. A zero argument disables channel 1 and holds
 * PB13 low as GPIO, removing the servo control pulses.
 */
/************************************************************************/


void servo_set_cdty(uint32_t cdty)
{
    // Clamp the pulse width to the configured servo limits.
    if (cdty < SERVO_CDTY_MIN) cdty = SERVO_CDTY_MIN;
    if (cdty > SERVO_CDTY_MAX) cdty = SERVO_CDTY_MAX;
 
    current_cdty = cdty;
 
    /*
     * Queue the new duty in PWM_CDTYUPD for the next PWM period
     * (datasheet 38.6.5.3, page 997).
     */
    PWM->PWM_CH_NUM[1].PWM_CDTYUPD = cdty;
}
/************************************************************************/
/*
 * servo_set_cdty(): Clamps the requested pulse width in PWM counts,
 * records it, and queues it for the next PWM period.
 */
/************************************************************************/
 
 #define JOY_DEADBAND  8 // Filtered offsets strictly below 8 map to centre.



void servo_set_from_joystick(uint8_t x)
{
	/*
	 * ---- Low-pass filter ----------------------------------------
	 */
	static int16_t x_filtered = JOY_CENTER; // Filter state carried across calls.

	x_filtered = x_filtered + ((int16_t)x - x_filtered) / 4; // Move one quarter of the remaining distance using integer division.

	/*
	 * ---- Dead band around the centre ----------------------------
	 */
	int16_t x_use = x_filtered;
	if (x_use > JOY_CENTER - JOY_DEADBAND &&
	x_use < JOY_CENTER + JOY_DEADBAND) {
		x_use = JOY_CENTER;
	}

    int32_t cdty;
	
	
    if (x  >= JOY_CENTER) {
        /*
         * Raw input at or above centre selects the upper-half scale.
         * The calculation uses the filtered input x_use.
         */
        cdty = SERVO_CDTY_CENTER  + ((int32_t)x_use  - JOY_CENTER) * (SERVO_CDTY_MAX - SERVO_CDTY_CENTER)/ (JOY_MAX - JOY_CENTER);
    } else {
        /*
         * Raw input below centre selects the lower-half scale.
         * The calculation uses the filtered input x_use.
         */
        cdty = SERVO_CDTY_CENTER - (JOY_CENTER - (int32_t)x_use) * (SERVO_CDTY_CENTER - SERVO_CDTY_MIN)  / (JOY_CENTER - JOY_MIN);
    }
 
    servo_set_cdty((uint32_t)cdty);
}
/************************************************************************/
/*
 * servo_set_from_joystick(): Smooths each input sample and snaps
 * small filtered offsets to centre. The raw sample selects the
 * mapping slope while the filtered sample supplies the offset.
 * Passes the resulting pulse width to servo_set_cdty() for clamping.
 */
/************************************************************************/
 
uint32_t servo_get_cdty(void)
{
    return current_cdty;
}
/************************************************************************/
/*
 * servo_get_cdty(): Returns the last clamped duty command in PWM
 * counts, including while the output is disabled.
 */
/************************************************************************/







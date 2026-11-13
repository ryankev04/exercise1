/*
 * motor.c
 *
 * Created: 26.10.2026 22:23:22
 *  Author: ngabo
 */ 
#include "sam.h"
#include "motor.h" 


#define JOY_CENTER       126 // Neutral joystick X reading.
#define JOY_MAX_DIFF     75 // Deflection that reaches the duty cap.
#define MOTOR_CPRD 4200 // 84 MHz / 4200 = 20 kHz
#define MOTOR_DEADBAND  10 // Deflections below this magnitude give zero duty.
#define MOTOR_MAX       (MOTOR_CPRD / 3) // Duty cap in PWM counts (1400 of 4200, about 33%).
void motor_init (void) {
	PMC->PMC_PCER1 = (1u << (ID_PWM - 32));
    // The motor and servo share the PWM peripheral clock.
	
    // ENABLE pin: PWM channel 0 on PB12
    // Turn channel 0 off while we configure it, and wait until it is off
    PWM->PWM_DIS = PWM_DIS_CHID0;
    while (PWM->PWM_SR & PWM_SR_CHID0) { }

    /*
     * Channel mode:
     *   CPRE = MCK     -> 84 000 000 counts per second (no divider)
     *   CPOL = 1       -> CDTY = high time = time the motor is ON
     *   left aligned (default)
     * Period: CPRD = 84 000 000 / 20 000 = 4200 -> 20 kHz.
     */
    PWM->PWM_CH_NUM[0].PWM_CMR  = PWM_CMR_CPRE_MCK | PWM_CMR_CPOL;
    PWM->PWM_CH_NUM[0].PWM_CPRD = MOTOR_CPRD;

    // Start STOPPED: 0 counts high = 0 % = motor off
    PWM->PWM_CH_NUM[0].PWM_CDTY = 0;

    // Turn channel 0 on
    PWM->PWM_ENA = PWM_ENA_CHID0;

    /*
     * Connect PB12 to the PWM: peripheral B = PWMH0
     *   PIO_ABSR is READ-WRITE and holds all 32 pins of port B, so "|="
     *   sets only bit 12 and keeps bit 13 (servo, PB13) as it was.
     *   With "=" the servo pin would switch to peripheral A and stop.
     *   PIO_PDR is WRITE-ONLY -> "=" is fine.
     */
    PIOB->PIO_ABSR |= PIO_PB12;
    PIOB->PIO_PDR   = PIO_PB12;

    /*
     * ---- PHASE pin: PC23 as a normal digital output -----------------
     */
    PIOC->PIO_PER  = PIO_PC23; // PIO Enable: the pin is a normal I/O pin
    PIOC->PIO_OER  = PIO_PC23; // Output Enable: the pin is an output
    PIOC->PIO_CODR = PIO_PC23; // Clear Output Data: start with 0
	
	
}
/************************************************************************/
/*
 * motor_init(): Enables the PWM clock and configures channel 0
 * for a 20 kHz active-high ENABLE signal on PB12 at 84 MHz MCK.
 * Starts with zero duty and sets PC23 (PHASE) low.
 */
/************************************************************************/

void motor_set(uint8_t dir, uint32_t duty)
{
    // Limit the requested duty to the driver cap.
    if (duty > MOTOR_MAX) duty = MOTOR_MAX;

    // Direction on the PHASE pin
    if (dir)
	 {
        PIOC->PIO_SODR = PIO_PC23; // Set Output Data   -> pin = 1 (around 3.3V)
    } 
	else {
        PIOC->PIO_CODR = PIO_PC23; // Clear Output Data -> pin = 0 (0 V)
    }

    /*
     * Speed: write the UPDATE register, so the new duty starts at the
     * beginning of the next PWM period (no cut-off pulses).
     */
    PWM->PWM_CH_NUM[0].PWM_CDTYUPD = duty;
}
/************************************************************************/
/*
 * motor_set(): Sets PHASE low for dir = 0 and high for a nonzero
 * dir. Limits duty to MOTOR_MAX and queues it for the next PWM
 * period. Zero duty disables motor drive.
 */
/************************************************************************/

void motor_set_from_joystick(uint8_t x)
{
	int16_t diff = (int16_t)x - JOY_CENTER; // distance from centre (+ right, - left)
	uint8_t dir;

	if (diff < 0) 
	{ // sign -> direction
		dir  = 0;
		diff = -diff;
	}
	else 
	{
		dir  = 1;
	}

	if (diff < MOTOR_DEADBAND)
	{ // small movement or vibration near the centre -> stop
		diff = 0;
	}
	// safety checks to not go over the max
	if (diff > JOY_MAX_DIFF)
	{
		diff = JOY_MAX_DIFF;
	}

	/*
	 * Distance 0 .. 75 -> duty 0 .. MOTOR_MAX
	 *    Multiply first, then divide (integer maths, otherwise we lose
	 *    everything after the decimal point).
	 */
	uint32_t duty = (uint32_t)diff * MOTOR_MAX / JOY_MAX_DIFF; // Scale deflection to PWM duty counts.

	motor_set(dir, duty); // send to the hardware
}
/************************************************************************/
/*
 * motor_set_from_joystick(): Maps joystick X to direction and
 * PWM duty. Readings within 9 counts of 126 give zero duty.
 * A deflection of 75 counts or more reaches the duty cap.
 *
 * For example, X = 164 selects dir = 1 and 709 PWM counts.
 * X = 90 selects dir = 0 and 672 PWM counts.
 */
/************************************************************************/

void motor_safe_off(void)
{
    PIOB->PIO_PUDR = PIO_PB12; // pull-up OFF (PIO_PUDR, page 653)
    PIOB->PIO_CODR = PIO_PB12; // output value = 0 ...
    PIOB->PIO_OER  = PIO_PB12; // ... THEN make it an output (so it is never 1)
    PIOB->PIO_PER  = PIO_PB12; // PIO controls the pin (until motor_init gives it to the PWM)

    PIOC->PIO_PUDR = PIO_PC23; // same for PHASE: pull-up off, output 0
    PIOC->PIO_CODR = PIO_PC23;
    PIOC->PIO_OER  = PIO_PC23;
    PIOC->PIO_PER  = PIO_PC23;
}
/************************************************************************/
/*
 * motor_safe_off(): Disables the pull-ups and takes GPIO control
 * of ENABLE and PHASE with both outputs low. The output latches
 * are cleared before the pins are switched to GPIO control.
 */
/************************************************************************/
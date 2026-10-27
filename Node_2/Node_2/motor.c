/*
 * motor.c
 *
 * Created: 26.10.2026 22:23:22
 *  Author: ngabo
 */ 
#include "sam.h"
#include "motor.h"


#define JOY_CENTER       126
#define JOY_MAX_DIFF     75              
#define MOTOR_CPRD 4200   // 84 MHz / 4200 = 20 kHz
#define MOTOR_DEADBAND  10
#define MOTOR_MAX       (MOTOR_CPRD / 4)   // 25 % for the first test
void motor_init (void) {
	/* NOTE: the PWM clock (PMC_PCER1, ID_PWM) is turned on in servo_init()
     * in pwm.c. servo_init() MUST be called before motor_init(),
     * otherwise the PWM registers below are not written and the motor
     * does nothing. 
	  */
	
	// ENABLE pin: PWM channel 0 on PB12 
    // Turn channel 0 off while we configure it, and wait until it is off
    PWM->PWM_DIS = PWM_DIS_CHID0;
    while (PWM->PWM_SR & PWM_SR_CHID0) { }

    /* Channel mode:
     *   CPRE = MCK     -> 84 000 000 counts per second (no divider)
     *   CPOL = 1       -> CDTY = high time = time the motor is ON
     *   left aligned (default)
     * Period: CPRD = 84 000 000 / 20 000 = 4200 -> 20 kHz.
     * 20 kHz is above what we can hear, so the motor does not whine,
     * and below the 50 kHz max from the motor shield document.       
	   */
    PWM->PWM_CH_NUM[0].PWM_CMR  = PWM_CMR_CPRE_MCK | PWM_CMR_CPOL;
    PWM->PWM_CH_NUM[0].PWM_CPRD = MOTOR_CPRD;

    // Start STOPPED: 0 counts high = 0 % = motor off
	// Period: 4200 counts = 50 us = 20 kHz check  pwm.h to get the idea on how to calculate CDTY
    PWM->PWM_CH_NUM[0].PWM_CDTY = 0;

    // Turn channel 0 on
    PWM->PWM_ENA = PWM_ENA_CHID0;

    /* Connect PB12 to the PWM: peripheral B = PWMH0
     *   PIO_ABSR is READ-WRITE and holds all 32 pins of port B, so "|="
     *   sets only bit 12 and keeps bit 13 (servo, PB13) as it was.
     *   With "=" the servo pin would switch to peripheral A and stop.
     *   PIO_PDR is WRITE-ONLY -> "=" is fine.  
	  */
    PIOB->PIO_ABSR |= PIO_PB12;
    PIOB->PIO_PDR   = PIO_PB12;

    /* ---- PHASE pin: PC23 as a normal digital output ----------------- */
    PIOC->PIO_PER  = PIO_PC23;     // PIO Enable: the pin is a normal I/O pin
    PIOC->PIO_OER  = PIO_PC23;     // Output Enable: the pin is an output
    PIOC->PIO_CODR = PIO_PC23;     // Clear Output Data: start with 0
	
	
}

/* dir:  0 or 1 (which way the motor turns)
 * duty: 0 (stopped) ... MOTOR_CPRD (full speed)
   */
void motor_set(uint8_t dir, uint32_t duty)
{
    // Safety: never more than 100 %
    if (duty > MOTOR_CPRD) duty = MOTOR_CPRD;

    // Direction on the PHASE pin
    if (dir)
	 {
        PIOC->PIO_SODR = PIO_PC23;     // Set Output Data   -> pin = 1 (around 3.3V)
    } 
	else {
        PIOC->PIO_CODR = PIO_PC23;     // Clear Output Data -> pin = 0 (0 V)
    }

   /* Speed: write the UPDATE register, so the new duty starts at the
    * beginning of the next PWM period (no cut-off pulses).       
	*/
    PWM->PWM_CH_NUM[0].PWM_CDTYUPD = duty;
}


/* Joystick X -> direction + speed
 *
 * Examples (MOTOR_MAX = 2100):
 *   x = 126 -> diff =   0 -> stopped
 *   x = 130 -> diff =  +4 -> inside dead band -> stopped
 *   x = 164 -> diff = +38 -> dir 1, duty = 38 * 2100 / 75 = 1064
 *   x = 201 -> diff = +75 -> dir 1, duty = 75 * 2100 / 75 = 2100
 *   x =  90 -> diff = -36 -> dir 0, duty = 36 * 2100 / 75 = 1008
 *   x =  54 -> diff = -72 -> dir 0, duty = 72 * 2100 / 75 = 2016     
  */
void motor_set_from_joystick(uint8_t x)
{
	int16_t diff = (int16_t)x - JOY_CENTER;    //  distance from centre (+ right, - left)
	uint8_t dir;

	if (diff < 0) 
	{   //  sign -> direction
		dir  = 0;
		diff = -diff;
	}
	else 
	{
		dir  = 1;
	}

	if (diff < MOTOR_DEADBAND)
	{   // small movement or vibration near the centre -> stop
		diff = 0;
	}
	// safety checks to not go over the max
	if (diff > JOY_MAX_DIFF)
	{
		diff = JOY_MAX_DIFF;
	}

  // maybe not optimal should be checked 
  /*  Distance 0 .. 75 -> duty 0 .. MOTOR_MAX
     *    Multiply first, then divide (integer maths, otherwise we lose
     *    everything after the decimal point).  
	  */
	uint32_t duty = (uint32_t)diff * MOTOR_MAX / JOY_MAX_DIFF;   //  distance -> speed

	motor_set(dir, duty);                      //  send to the hardware
}
/*
 * Encoder.c
 *
 * Created: 26.10.2026 15:44:03
 *  Author: ngabo
 */ 
#include "sam.h"
#include "Encoder.h"

 void encoder_init (void)
 {
	 
	/*
     *  Turn on the clock to TC6 (= TC2 channel 0)
     * Every peripheral is OFF until the PMC gives it a clock.
     * TC6 has peripheral ID 33. IDs 0..31 are in PMC_PCER0 and IDs
     * 32..63 are in PMC_PCER1, so the bit is 33 - 32 = 1 in PCER1.
     *
     * PMC_PCER1 is a WRITE-ONLY register: writing 1 turns a clock on,
     * writing 0 does nothing. So "=" is correct (not "|="), it does not
     * turn off the other peripherals (PWM, ADC, CAN).             
	   */
	 PMC->PMC_PCER1 = (1u << (ID_TC6 - 32));

	    /* 
     *  Give pins PC25 (A) and PC26 (B) to the timer
     * Each pin can be a normal I/O pin (PIO) or belong to a peripheral.
     * TIOA6/TIOB6 are "peripheral B" on these pins (Table 36-4).
     *
     * PIO_ABSR: bit = 0 -> peripheral A, bit = 1 -> peripheral B.
     *   It is a READ-WRITE register that holds the setting for all 32
     *   pins of port C, so we use "|=" to set only our two bits and
     *   leave the other pins as they were.
     *
     * PIO_PDR (PIO Disable Register): takes the pins away from normal
     *   I/O so the peripheral selected above controls them.
     *   WRITE-ONLY register.              
	          */
	 PIOC->PIO_ABSR |= PIO_PC25 | PIO_PC26;     // read-write register -> |=
	 PIOC->PIO_PDR   = PIO_PC25 | PIO_PC26;     // write-only register -> =
	 
	 
	
	 /* 
     * Channel mode (TC_CMR) - settings required by page 876
     * For position measurement the datasheet says channel 0 must be:
     *   - in CAPTURE mode (WAVE = 0). WAVE is bit 15; we simply do not
     *     set it, so it is 0.
     *   - TCCLKS = XC0: the counter counts the pulses coming from the
     *     quadrature decoder (XC0) instead of a clock from MCK
     *     (page 871: "TCCLKS ... must be configured to select XC0").
     *   - ETRGEDG = RISING: external trigger on the rising edge.
     *   - ABETRG = 1: TIOA is used as the external trigger.
     * (Unlike timer.c, this channel does NOT count time - it counts
     *  encoder edges.)                                               */
	 TC2->TC_CHANNEL[0].TC_CMR = TC_CMR_TCCLKS_XC0| TC_CMR_ETRGEDG_RISING | TC_CMR_ABETRG;

    /* 
     *  Block mode (TC_BMR) - turns the quadrature decoder on
     * TC_BMR = Timer Counter BLOCK Mode Register. It is shared by all
     * three channels of the TC2 block (6, 7, 8), which is why it is
     * TC2->TC_BMR and not TC2->TC_CHANNEL[0].xxx
     *
     *   QDEN   = Quadrature Decoder Enable: the decoder hardware reads
     *            A and B and tells the counter to go up or down.
     *   POSEN  = Position Enable: channel 0 measures POSITION
     *            (the alternative, SPEEDEN, measures speed instead).
     *            The datasheet says QDEN needs POSEN or SPEEDEN.
     *   EDGPHA = count EDGes on both PHA and PHB (A and B).
     *            0 = count only edges on A
     *            1 = count edges on A AND B -> 4 counts per encoder
     *                period instead of 1 = 4x better resolution.   */
	 
	 TC2->TC_BMR = TC_BMR_QDEN | TC_BMR_POSEN | TC_BMR_EDGPHA;

	 /*
     *  Start the counter and reset it to 0
     * TC_CCR = Channel Control Register (write-only -> "=")
     *   CLKEN = clock enable: the counter is allowed to count.
     *   SWTRG = software trigger: resets the counter value to 0 now.
     * From here the position is 0
	   */
	 TC2->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
 }

int32_t encoder_read (void)
{

}
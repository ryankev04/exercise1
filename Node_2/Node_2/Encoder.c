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
	 
	 PMC->PMC_PCER1 = (1u << (ID_TC6 - 32));

	 PIOC->PIO_ABSR |= PIO_PC25 | PIO_PC26; // Select peripheral B for PC25 and PC26.
	 PIOC->PIO_PDR   = PIO_PC25 | PIO_PC26; // Hand the two pins over to the peripheral.
	 
	 
	
	 TC2->TC_CHANNEL[0].TC_CMR = TC_CMR_TCCLKS_XC0| TC_CMR_ETRGEDG_RISING | TC_CMR_ABETRG;

	 
	 TC2->TC_BMR = TC_BMR_QDEN | TC_BMR_POSEN | TC_BMR_EDGPHA;

	 TC2->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
 }
/************************************************************************/
/*
 * encoder_init(): Enables the TC6 clock and routes encoder phases A and
 * B from PC25 and PC26 to TIOA6 and TIOB6. TC2 channel 0 uses capture
 * mode with XC0. QDEN and POSEN enable quadrature position counting, and
 * EDGPHA selects both phases for edge detection. CLKEN and SWTRG enable
 * the counter and start it at zero.
 */
/************************************************************************/

int32_t encoder_read(void)
{
	uint32_t raw = TC2->TC_CHANNEL[0].TC_CV;
	int32_t  position = (int32_t)raw;
	return position;
}
/************************************************************************/
/*
 * encoder_read(): Reads the current position count relative to the
 * software trigger in encoder_init(). The counter increments or
 * decrements with encoder direction. On this target, casting the wrapped
 * 32-bit count to int32_t represents one count below zero as -1.
 */
/************************************************************************/
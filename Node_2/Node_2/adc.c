/*
 * adc.c
 *
 * Created: 13.10.2026 14:49:33
 *  Author: ngabo
 */ 
#include "sam.h"
#include "adc.h"

void adc_init(void)
{
    PMC->PMC_PCER1 = (1u << (ID_ADC - 32)); // Enable the ADC peripheral clock (PCER1 bit 5).

    ADC->ADC_CR = ADC_CR_SWRST; // Reset the ADC before configuring it.

    ADC->ADC_MR = ADC_MR_PRESCAL(4)| ADC_MR_STARTUP_SUT512 | ADC_MR_SETTLING_AST3 | ADC_MR_TRACKTIM(0)| ADC_MR_TRANSFER(1) | ADC_MR_FREERUN_ON;

    ADC->ADC_CHER = (1u << ADC_IR_CHANNEL); // Enable AD7 (PA16, Arduino Due A0).

    ADC->ADC_CR = ADC_CR_START;
}
/************************************************************************/
/*
 * adc_init(): Enables the ADC clock, resets the peripheral, configures
 * free-running mode, and enables the IR input. START launches the first
 * conversion here. With an 84 MHz master clock and PRESCAL = 4, the ADC
 * clock is 8.4 MHz.
 */
/************************************************************************/

uint16_t adc_read(uint8_t channel)
{
    return (uint16_t)(ADC->ADC_CDR[channel] & 0x0FFF);
}
/************************************************************************/
/*
 * adc_read(): Returns the low 12 bits of the selected channel data
 * register (0..4095). This reads the stored result immediately, without
 * waiting for a fresh conversion. The caller supplies a valid channel
 * index. adc_init() enables only ADC_IR_CHANNEL.
 */
/************************************************************************/
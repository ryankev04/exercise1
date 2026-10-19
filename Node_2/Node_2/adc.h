/*
 * adc.h
 *
 * Created: 13.10.2026 14:49:45
 *  Author: ngabo
 *
 * The IR signal is connected to Arduino Due pin A0.
 *   A0 = PA16 = ADC channel 7 (AD7)   (datasheet Table 43-3, page 1320)
 *
 * !! Input range is 0 - 3.3 V. Never connect 5 V to A0. !!
 */ 

#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>

#define ADC_IR_CHANNEL   7      /* A0 = AD7 */

/* Set up the ADC to convert channel 7 continuously (free-run mode) */
void adc_init(void);

/* Newest result for a channel: 0 .. 4095  (0 V .. 3.3 V) */
uint16_t adc_read(uint8_t channel);

#endif // ADC_H_ 
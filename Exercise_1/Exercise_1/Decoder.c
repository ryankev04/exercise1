/*
 * Decoder.c
 *
 * Created: 09.09.2026 13:27:57
 *  Author: ngabo
 */ 


#include "usart.h"
#include "Decoder.h"
#include "ADC.h"

#include <util/delay.h>


void decoder_test(void)
{
	volatile uint8_t *adc_addr = (uint8_t *) ADC_ADDR; // 0x1000, A11:A10 = 00.
	volatile uint8_t *sram_addr = (uint8_t *) SRAM_START; // 0x1400, A11:A10 = 01.
	volatile uint8_t *sram_addr2 = (uint8_t *) 0x1800; // A11:A10 = 10.
	volatile uint8_t *sram_addr3 = (uint8_t *) 0x1C00; // A11:A10 = 11.
	
	while (1)
	{
	
	
	printf("Accessing ADC range (0x%04x)\n", ADC_ADDR);
	*adc_addr = 0xAA; // Write a test byte to produce a bus cycle.
	_delay_ms(1000);  // Space out the probe pulses by one second.
	
	printf("Accessing SRAM range (0x%04x)\n", SRAM_START);
	*sram_addr = 0x55;
	
	_delay_ms(1000);
	
	printf("Accessing SRAM region 2 (0x%04x)\n", (uint16_t)sram_addr2);
	*sram_addr2 = 0x55;
	_delay_ms(1000);

	printf("Accessing SRAM region 3 (0x%04x)\n", (uint16_t)sram_addr3);
	*sram_addr3 = 0x55;
	_delay_ms(1000);

	}
	
	
}
/************************************************************************/
/*
 * decoder_test(): Repeatedly writes one byte at each of four external
 * addresses so the chip-select and write pulses can be probed.
 *
 * The addresses cover all four combinations of A11:A10 with A12 high.
 * Each message identifies the address before the write. The one-second
 * pauses separate the bus pulses for probing. This test runs forever,
 * overwrites the probed SRAM locations, and performs no readback.
 */
/************************************************************************/

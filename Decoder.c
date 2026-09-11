/*
 * Decoder.c
 *
 * Created: 09.09.2026 13:27:57
 *  Author: ngabo
 */ 


#include "usart.h"
#include "Decoder.h"

#include <util/delay.h>

#define ADC_ADDR 0x1000   // anywhere between 0x1000 to 0x17FF
void decoder_test(void)
{
	volatile uint8_t *adc_addr = (uint8_t *) ADC_ADDR; // A11=0, A10=1
	volatile uint8_t *sram_addr = (uint8_t *) SRAM_START;
	volatile uint8_t *sram_addr2 = (uint8_t *) 0x1800;   // A11=1, A10=0
	volatile uint8_t *sram_addr3 = (uint8_t *) 0x1C00;   // A11=1, A10=1
	
	while(1)
	{
	printf("Accessing ADC range (0x%04x)\n", ADC_ADDR);
	*adc_addr = 0xAA;  // here i write a dummy address write just to toggle the bus
	_delay_ms(1000);  //holding  long enough to probe the ADC chip select 
	
	printf("Accessing SRAM range (0x%04x)\n", SRAM_START);
	*sram_addr = 0x55;
	
	_delay_ms(1000);  //holding  long enough to probe the SRAM chip select
	
	printf("Accessing SRAM region 2 (0x%04x)\n", (uint16_t)sram_addr2);
	*sram_addr2 = 0x55;
	_delay_ms(1000);

	printf("Accessing SRAM region 3 (0x%04x)\n", (uint16_t)sram_addr3);
	*sram_addr3 = 0x55;
	_delay_ms(1000);
	}
	
	
	
}

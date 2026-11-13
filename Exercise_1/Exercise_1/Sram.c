/*
 * Sram.c
 *
 * Created: 08.09.2026 16:41:32
 *  Author: ngabo
 */ 

#include <stdlib.h>


#include "usart.h"
#include "Sram.h"

#include <util/delay.h>

void Xmem_init(void)
{
	SFIOR |= (1 << XMM2); // Set the high-address mask bit XMM2.
	MCUCR |= (1 << SRE);  // enable Xmem
}
/************************************************************************/
/*
 * Xmem_init(): Sets XMM2 and enables the external memory interface with
 * SRE. With XMM1 and XMM0 clear, XMEM releases PC7 through PC4. See
 * ATmega162 datasheet, Table 4.
 */
/************************************************************************/

void Latch_test (void)
{
	for (uint8_t i = 0 ; i < 8; i++)
	{
		volatile uint8_t *addr = (uint8_t *)(SRAM_START +(1 << i));
		*addr = 0xFF; // Test byte used to generate a bus write.
		_delay_ms(300);
	}
}
/************************************************************************/
/*
 * Latch_test(): Writes once at each one-bit offset from SRAM_START for i
 * = 0..7. The 300 ms pauses separate the writes for probing.
 */
/************************************************************************/

void SRAM_test(void)
{
	volatile char *ext_ram = (char *) SRAM_START; // Start address for the SRAM
	uint16_t ext_ram_size = SRAM_SIZE;
	uint16_t write_errors = 0;
	uint16_t retrieval_errors = 0;
	printf("Starting SRAM test...\n");
	// Use the current PRNG state to obtain the seed for both passes.
	uint16_t seed = rand();
	// Write phase: Immediately check that the correct value was stored
	srand(seed);
	for (uint16_t i = 0; i < ext_ram_size; i++) {
		uint8_t some_value = rand();
		ext_ram[i] = some_value;
		uint8_t retreived_value = ext_ram[i];
		if (retreived_value != some_value) {
			printf("Write phase error: ext_ram[%4d] = %02X (should be %02X)\n", i, retreived_value, some_value);
			write_errors++;
		}
	}
	// Retrieval phase: Check that no values were changed during or after the write phase
	srand(seed);
	// reset the PRNG to the state it had before the write phase
	for (uint16_t i = 0; i < ext_ram_size; i++) {
		uint8_t some_value = rand();
		uint8_t retreived_value = ext_ram[i];
		if (retreived_value != some_value) {
			printf("Retrieval phase error: ext_ram[%4d] = %02X (should be %02X)\n", i, retreived_value, some_value);
			retrieval_errors++;
		}
	}
	printf("SRAM test completed with \n%4d errors in write phase and \n%4d errors in retrieval phase\n\n", write_errors, retrieval_errors);
}
/************************************************************************/
/*
 * SRAM_test(): Overwrites the configured SRAM region with a pseudo-random
 * sequence. Checks each write immediately, then reseeds and checks the
 * whole region against the same sequence. Prints separate error counts.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * void SRAM_single_test(void)
 * {
 * 	volatile uint8_t *addr = (uint8_t *)0x1800;
 * 	*addr = 0xAA;
 * 	uint8_t result = *addr;
 * 	printf("Wrote 0xAA to 0x1800, read back: 0x%02X\n", result);
 * }
 */
/************************************************************************/

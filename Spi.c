/*
 * Spi.c
 *
 * Created: 22.09.2026 10:42:53
 *  Author: ngabo
 */ 
#include "Spi.h"


/*
 *   Set direction (DDRx) -- turns the pin from a passive input into
 *      an active output the AVR is driving.
 *   Set value (PORTx) -- once a pin is an output, this controls the
 *     actual voltage on it (1=high, 0=low). Writing PORTx BEFORE the
 *     pin is an output would do something different -- it would just
 *     enable/disable that pin's internal pull-up resistor instead.
 */
void spi_master_init(void)
{
	// MOSI and SCK must be outputs for Master mode
	SPI_DDR |= (1 << SPI_MOSI) | (1 << SPI_SCK);

	SS_DISPLAY_DDR |= (1 << SS_DISPLAY_PIN);
	SS_IO_DDR      |= (1 << SS_IO_PIN);


// here we simply set DIS_Cs and io_CS to high because the pins are originally low when you start
// setting them high will just deselect them
	spi_deselect_all();

	// SPE: enable SPI. MSTR: master mode. SPR0: clock = fosc/16. clock = 307.2kHz
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
}

// No SPI slave may be selected at the same time as another slave.
void spi_deselect_all(void)
{
	SS_DISPLAY_PORT |= (1 << SS_DISPLAY_PIN);
	SS_IO_PORT      |= (1 << SS_IO_PIN);
}

// Picks ONE device to "wake up" and listen to the SPI bus.
// CS pins are active-LOW, so pulling one LOW = select this device.
void spi_select_slave(spi_slave_t slave)
{
	// Establish a safe idle state before selecting the requested device.
	spi_deselect_all();
	
	switch (slave) {
		case SPI_SLAVE_DISPLAY:
		// &= bitwise AND  ~=bitwise NOT
		SS_DISPLAY_PORT &= ~(1 << SS_DISPLAY_PIN);  // PB4 LOW -> OLED selected
		break;
		case SPI_SLAVE_IO:
		SS_IO_PORT &= ~(1 << SS_IO_PIN);  // PB1 LOW -> IO board selected
		break;
	}
}

// Releases a device so it stops listening to the SPI bus.
// Driving CS back HIGH = deselect this device.
void spi_deselect_slave(spi_slave_t slave)
{
	switch (slave) {
		case SPI_SLAVE_DISPLAY:
		SS_DISPLAY_PORT |= (1 << SS_DISPLAY_PIN); // PB4 HIGH -> OLED deselected
		break;
		case SPI_SLAVE_IO:
		SS_IO_PORT |= (1 << SS_IO_PIN);   // PB1 HIGH -> IO board deselected
		break;
	}
}


/*
* spi_transfer_byte(): SPI is full-duplex -- writing SPDR starts the clock
* and simultaneously shifts a byte OUT (to the slave) and IN (from the slave).
*/
uint8_t spi_transfer_byte (uint8_t data)
{
	SPDR = data;
	while (!(SPSR & (1 << SPIF))) { /* wait for transmission complete */ }
	return SPDR;
}

// Sends a whole list of bytes, one at a time, in order.
void spi_write_bytes(const uint8_t *data, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		spi_transfer_byte(data[i]);
	}
}

// Receives a whole list of bytes, one at a time.
void spi_read_bytes(uint8_t *buffer, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		buffer[i] = spi_transfer_byte(0x00);
		// SPI can't receive without also sending , so we send a
		// meaningless 0x00 just to make the clock tick, and keep
		// whatever byte comes back in return.
	}
}



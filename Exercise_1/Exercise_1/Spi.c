/*
 * Spi.c
 *
 * Created: 22.09.2026 10:42:53
 *  Author: ngabo
 */ 
#include "Spi.h"


void spi_master_init(void)
{
	// MOSI and SCK must be outputs for Master mode
	SPI_DDR |= (1 << SPI_MOSI) | (1 << SPI_SCK);

	SS_DISPLAY_DDR |= (1 << SS_DISPLAY_PIN);
	SS_IO_DDR      |= (1 << SS_IO_PIN);
	SS_CAN_DDR |= (1 << SS_CAN_PIN);


	spi_deselect_all(); // Drive all three active-low chip selects high.
	                    

	// Enable the SPI master and select the prescaler.
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
}
/************************************************************************/
/*
 * spi_master_init(): Configures MOSI, SCK, and the three chip selects as
 * outputs. Enables the SPI master in mode 0, MSB first. With SPI2X clear,
 * SPR0 selects clk_I/O divided by 16.
 */
/************************************************************************/

void spi_deselect_all(void)
{
	SS_DISPLAY_PORT |= (1 << SS_DISPLAY_PIN);
	SS_IO_PORT      |= (1 << SS_IO_PIN);
	SS_CAN_PORT |= (1 << SS_CAN_PIN); 
}
/************************************************************************/
/*
 * spi_deselect_all(): Drives all three active-low chip selects high.
 */
/************************************************************************/

void spi_select_slave(spi_slave_t slave)
{
	// Establish a safe idle state before selecting the requested device.
	spi_deselect_all();
	
	switch (slave) {
		case SPI_SLAVE_DISPLAY:
		// &= bitwise AND  ~=bitwise NOT
		SS_DISPLAY_PORT &= ~(1 << SS_DISPLAY_PIN); // PB4 LOW -> OLED selected
		break;
		case SPI_SLAVE_IO:
		SS_IO_PORT &= ~(1 << SS_IO_PIN); // PB0 LOW -> IO board selected
		break;
		case SPI_SLAVE_CAN:
		SS_CAN_PORT &= ~(1 << SS_CAN_PIN); // PB1 LOW -> CAN MCP2515 selected
		break;
	}
}
/************************************************************************/
/*
 * spi_select_slave(): Deselects all devices, then pulls the requested
 * chip select low.
 */
/************************************************************************/

void spi_deselect_slave(spi_slave_t slave)
{
	switch (slave) {
		case SPI_SLAVE_DISPLAY:
		SS_DISPLAY_PORT |= (1 << SS_DISPLAY_PIN); // PB4 HIGH -> OLED deselected
		break;
		case SPI_SLAVE_IO:
		SS_IO_PORT |= (1 << SS_IO_PIN); // PB0 HIGH -> IO board deselected
		break;
		case SPI_SLAVE_CAN:
		SS_CAN_PORT |= (1 << SS_CAN_PIN); // PB1 HIGH -> CAN MCP2515 deselected
		break;
	}
}
/************************************************************************/
/*
 * spi_deselect_slave(): Drives the selected device chip-select line high
 * to end its transaction.
 */
/************************************************************************/

uint8_t spi_transfer_byte (uint8_t data)
{
	SPDR = data;
	while (!(SPSR & (1 << SPIF))) { /* wait for transmission complete */ }
	return SPDR;
}
/************************************************************************/
/*
 * spi_transfer_byte(): Writes SPDR, waits for SPIF, and returns the byte
 * received during the transfer.
 */
/************************************************************************/

void spi_write_bytes(const uint8_t *data, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		spi_transfer_byte(data[i]);
	}
}
/************************************************************************/
/*
 * spi_write_bytes(): Sends len bytes in order. The caller handles device
 * selection.
 */
/************************************************************************/

void spi_read_bytes(uint8_t *buffer, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		buffer[i] = spi_transfer_byte(0x00);
	// SPI can't receive without also sending , so we send a
	// meaningless 0x00 just to make the clock tick, and keep
	// whatever byte comes back in return.
	}
}
/************************************************************************/
/*
 * spi_read_bytes(): Sends a dummy zero for each byte to clock in len
 * bytes. The caller handles device selection.
 */
/************************************************************************/


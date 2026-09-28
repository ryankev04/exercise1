/*
 * Can2515.c
 *
 * Created: 25.09.2026 14:05:14
 *  Author: ngabo
 */ 

#include "Can2515.h"
#include "Spi.h"

/*
* mcp2515_reset(): datasheet chapter12.2, p.65.
* single-byte instruction that requires selecting the device by
*  pulling CS low, sending the instruction byte and then raising CS.
* "highly recommended that the Reset command be sent... as part of
*  the power-on initialization sequence."
*/
void mcp2515_reset(void)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_RESET);
	spi_deselect_slave(SPI_SLAVE_CAN);
}


/*
* mcp2515_write(): symmetric to read  WRITE instruction, address,
* then the data byte to store there.
*/
void mcp2515_write(uint8_t address, uint8_t data)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_WRITE);
	spi_transfer_byte(address);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_CAN);
}

/*
* mcp2515_rts(): Request-To-Send, Table 12-1, p.66: "1000 0nnn".
* buffer_mask's low 3 bits select which TX buffer(s) to start sending 
* bit0=TXB0, bit1=TXB1, bit2=TXB2.
*/
void mcp2515_rts(uint8_t buffer_mask)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_RTS_BASE | (buffer_mask & 0x07));
	spi_deselect_slave(SPI_SLAVE_CAN);
}


/*
* mcp2515_read_status(): chapter 12  p.65-66, Quick polling command that
* reads several status bits for transmit and receive functions.
*/
uint8_t mcp2515_read_status(void)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_READ_STATUS);
	uint8_t status = spi_transfer_byte(0x00);
	spi_deselect_slave(SPI_SLAVE_CAN);
	return status;
}

/*
* mcp2515_bit_modify(): chaprer 12.10, p.66.
* followed by the address of the register, the mask byte and finally
*  the data byte. A '1' in the mask byte will allow a bit to change
*  A '1' in the data byte will set the bit and a '0' will clear the bit,
*  provided that the mask for that bit is set to a 1'.
*
* NOTE ( the datasheet p.66): only works on registers
* listed as bit-modifiable in the register map (Table 11-1, p.63) 
* using it elsewhere forces the mask to FFh, silently behaving as a
* full byte write instead. Check Table 11-1 p.63 to know which register 
*/
void mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_BIT_MODIFY);
	spi_transfer_byte(address);
	spi_transfer_byte(mask);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_CAN);
}
/*
 * Can2515.c
 *
 * Created: 25.09.2026 14:05:14
 *  Author: ngabo
 */ 
#include "usart.h"
#include "Can2515.h"
#include "Spi.h"
#include <util/delay.h>

static volatile uint8_t mcp_int_flag = 0; // Records a falling edge seen by the INT2 ISR.


ISR(INT2_vect)
{
	mcp_int_flag = 1;
}
/************************************************************************/
/*
 * ISR(INT2_vect): Records the MCP2515 interrupt for the main code.
 * The handler leaves SPI transfers to the caller so it cannot
 * interrupt an SPI transaction with another device selected.
 */
/************************************************************************/


void mcp2515_int_init(void)
{
	INT_CAN_DDR &= ~(1 << INT_CAN_PIN); // PE0 as input (MCP2515 INT is push-pull)
	EMCUCR &= ~(1 << ISC2);             // ISC2 = 0 -> falling edge (INT going low)
	GIFR    = (1 << INTF2);             // clear any old INT2 flag (write 1 to clear)
	GICR   |= (1 << INT2);              // enable INT2
// Global interrupts (sei) are already enabled in uart1_init().
}
/************************************************************************/
/*
 * mcp2515_int_init(): Sets PE0 as an input and enables the ATmega162
 * INT2 interrupt on the falling edge of the MCP2515's active-low INT.
 * It clears any stale AVR interrupt flag before enabling INT2.
 * Global interrupts are enabled separately by uart1_init().
 */
/************************************************************************/


uint8_t mcp2515_int_pending(void)
{
	uint8_t pending = mcp_int_flag || !(INT_CAN_PINR & (1 << INT_CAN_PIN)); // flag OR pin still low
	mcp_int_flag = 0;
	return pending;
}
/************************************************************************/
/*
 * mcp2515_int_pending(): Returns 1 if the ISR recorded an edge or
 * INT is still low. Checking the pin covers an interrupt that stays
 * asserted without another falling edge. This consumes the software
 * flag. The MCP2515 flags remain set until serviced over SPI.
 *
 * Reference: MCP2515 datasheet, pp. 51-53.
 */
/************************************************************************/


void mcp2515_reset(void)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_RESET);
	spi_deselect_slave(SPI_SLAVE_CAN);
}
/************************************************************************/
/*
 * mcp2515_reset(): Sends the one-byte RESET command with CS low.
 * The controller resets its registers and enters Configuration mode.
 * The caller allows time for the oscillator to start before reading
 * or writing registers.
 *
 * Reference: MCP2515 datasheet, pp. 55, 65.
 */
/************************************************************************/


void mcp2515_write(uint8_t address, uint8_t data)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_WRITE);
	spi_transfer_byte(address);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_CAN);
}
/************************************************************************/
/*
 * mcp2515_write(): Writes one byte to an MCP2515 register.
 * CS stays low while SPI sends the WRITE opcode, register address,
 * and data byte, then goes high to end the transaction.
 *
 * Reference: MCP2515 datasheet, pp. 65-67.
 */
/************************************************************************/


void mcp2515_rts(uint8_t buffer_mask)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_RTS_BASE | (buffer_mask & 0x07));
	spi_deselect_slave(SPI_SLAVE_CAN);
}
/************************************************************************/
/*
 * mcp2515_rts(): Requests transmission from the selected TX buffers.
 * Bits 0, 1, and 2 select TXB0, TXB1, and TXB2, respectively. The
 * three bits are part of the RTS opcode. The request leaves actual
 * transmission and bus arbitration to the controller.
 *
 * Reference: MCP2515 datasheet, pp. 15, 66, 68.
 */
/************************************************************************/


uint8_t mcp2515_read_status(void)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_READ_STATUS);
	uint8_t status = spi_transfer_byte(0x00);
	spi_deselect_slave(SPI_SLAVE_CAN);
	return status;
}
/************************************************************************/
/*
 * mcp2515_read_status(): Returns the SPI READ STATUS byte. The
 * controller packs RX flags, TX flags, and pending TX requests into
 * this byte. Its bit positions follow the command's response layout.
 *
 * Reference: MCP2515 datasheet, pp. 66, 69.
 */
/************************************************************************/


uint8_t mcp2515_read(uint8_t address)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_READ);
	spi_transfer_byte(address);
	uint8_t data = spi_transfer_byte(0x00); // dummy byte to clock data out
	spi_deselect_slave(SPI_SLAVE_CAN);
	return data;
}
/************************************************************************/
/*
 * mcp2515_read(): Returns one byte from the specified register.
 * After the READ opcode and address, the dummy SPI byte supplies
 * the clocks for the MCP2515 to shift out the register value.
 *
 * Reference: MCP2515 datasheet, pp. 65-67.
 */
/************************************************************************/


void mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data)
{
	spi_select_slave(SPI_SLAVE_CAN);
	spi_transfer_byte(MCP_BIT_MODIFY);
	spi_transfer_byte(address);
	spi_transfer_byte(mask);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_CAN);
}
/************************************************************************/
/*
 * mcp2515_bit_modify(): Updates selected bits in a register.
 * Each 1 in mask copies the corresponding bit from data. A 0
 * preserves the existing bit on registers that support Bit Modify.
 *
 * Use only the registers marked in the register map. Elsewhere the
 * controller forces the mask to 0xFF, making it a full-byte write.
 *
 * Reference: MCP2515 datasheet, pp. 63, 66, 68.
 */
/************************************************************************/


uint8_t mcp2515_init(uint8_t mode)
{
	
	mcp2515_reset(); // chip enters Configuration mode (12.2, p.65)
	_delay_ms(1);    // wait for oscillator start-up timer (8.1, p.55)

	// after reset we must be in Configuration mode.
	// If this fails, SPI wiring/CS or the MCP2515 clock is wrong.
	if ((mcp2515_read(MCP_CANSTAT) & MCP_MODE_MASK) != MCP_MODE_CONFIG)
	 {
		return 1;
	}

	// CNF registers are only writable in Configuration mode (10.1, p.59)
	mcp2515_write(MCP_CNF1, 0x01); // SJW = 1 TQ, BRP field = 1.
	mcp2515_write(MCP_CNF2, 0x8A); // BTLMODE=1, SAM=0, PS1 = 2 TQ, PropSeg = 3 TQ
	mcp2515_write(MCP_CNF3, 0x01); // PS2 = 2 TQ

	
	// Interrupts (Register 7-1, p.52): pull INT low when a message lands in RXB0.
	// Clear old flags first so INT starts released (high).
	mcp2515_write(MCP_CANINTF, 0x00);
	mcp2515_write(MCP_CANINTE, MCP_RX0I);
	mcp2515_write(MCP_RXB0CTRL, MCP_RXM_ANY);
	mcp2515_write(MCP_RXB1CTRL, MCP_RXM_ANY);
	
	// Request the operating mode (REQOP = upper 3 bits of CANCTRL)
	mcp2515_bit_modify(MCP_CANCTRL, MCP_MODE_MASK, mode);

	// Read OPMOD to check whether the requested mode took effect.
	if ((mcp2515_read(MCP_CANSTAT) & MCP_MODE_MASK) != mode) 
	{
		return 1;
	}
	return 0;
}
/************************************************************************/
/*
 * mcp2515_init(): Resets the controller, checks Configuration mode,
 * sets CAN bit timing and receive options, then requests mode.
 *
 * With a 16 MHz oscillator, BRP = 1 gives a time quantum of 250 ns.
 * SyncSeg (1), PropSeg (3), PS1 (2), and PS2 (2) total 8 TQ, giving
 * 500 kbit/s. The bus is sampled once, at 75% of the bit. SJW is 1 TQ.
 *
 * Both receive buffers have masks and filters disabled. Only RXB0
 * can assert INT under the interrupt enable mask written here.
 *
 * Returns 0 if CANSTAT reports the expected mode after reset and
 * again after the mode request. Returns 1 on either mismatch.
 *
 * Reference: MCP2515 datasheet, pp. 27-28, 41-45, 51-55, 59-61.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * mcp2515_int_init();
 */
/************************************************************************/

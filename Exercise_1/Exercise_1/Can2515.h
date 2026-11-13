/*
 * Can2515.h
 *
 * Created: 25.09.2026 14:05:23
 *  Author: ngabo
 */ 

#ifndef CAN2515_H_
#define CAN2515_H_

#include <stdint.h>

// MCP2515 SPI instruction opcodes, Table 12-1, p.66
#define MCP_RESET       0xC0 // Reset registers and enter Configuration mode.
#define MCP_READ        0x03 // Read from the register address that follows.
#define MCP_WRITE       0x02 // Write data to the register address that follows.
#define MCP_RTS_BASE    0x80 // Request transmission. Opcode bits 0..2 select TXB0..TXB2.
#define MCP_READ_STATUS 0xA0 // Read the packed RX flags, TX flags, and TX requests.
#define MCP_BIT_MODIFY  0x05 // Update masked bits in a supported register.


// Register addresses, Table 11-2, p.63
#define MCP_CANSTAT     0x0E // Status register. OPMOD reports the current mode.
#define MCP_CANCTRL     0x0F // Control register. REQOP requests a mode change.
#define MCP_CNF3        0x28 // Bit timing register. Sets PS2 when BTLMODE is set.
#define MCP_CNF2        0x29 // Bit timing register. Sets PropSeg, PS1, and sampling.
#define MCP_CNF1        0x2A // Bit timing register. Sets the prescaler and SJW.
#define MCP_CANINTE     0x2B // Interrupt enable register. Selects which flags can assert INT.
#define MCP_CANINTF     0x2C // Interrupt flag register. Records pending events.
#define MCP_RXB0CTRL    0x60 // Receive buffer 0 control register.
#define MCP_RXB1CTRL    0x70 // Receive buffer 1 control register.
#define MCP_RXM_ANY     0x60 // RXBnCTRL bits 6..5 = 11: disable masks and filters.

// CANINTE / CANINTF bits (same position in both registers), p.52-53
#define MCP_RX0I        0x01 // CANINTE bit 0: enable the RXB0 full interrupt.

// Operation modes, CANCTRL.REQOP / CANSTAT.OPMOD (upper 3 bits), p.60-61
#define MCP_MODE_MASK     0xE0 // Bits 7..5 of CANCTRL.REQOP or CANSTAT.OPMOD.
#define MCP_MODE_NORMAL   0x00 // Normal operation on the CAN bus.
#define MCP_MODE_LOOPBACK 0x40 // Internal transmit-to-receive test mode.
#define MCP_MODE_CONFIG   0x80 // Configuration mode, required for bit timing writes.

#define INT_CAN_DDR   DDRE // Port E direction register for setting PE0 as input.
#define INT_CAN_PORT  PORTE // Port E output latch and input pull-up control.
#define INT_CAN_PINR  PINE // Port E input register used to read INT level.
#define INT_CAN_PIN   PE0 // PE0/INT2, wired to the MCP2515 active-low INT output.

void mcp2515_int_init(void);
uint8_t mcp2515_init(uint8_t mode);
void mcp2515_reset(void);
void mcp2515_write(uint8_t address, uint8_t data);
void mcp2515_rts(uint8_t buffer_mask);
uint8_t mcp2515_read(uint8_t address);
uint8_t mcp2515_read_status(void);
void mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data);

uint8_t mcp2515_int_pending(void);

#endif // CAN2515_H_
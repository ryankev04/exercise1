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
#define MCP_RESET       0xC0
#define MCP_READ        0x03
#define MCP_WRITE       0x02
#define MCP_RTS_BASE    0x80   // OR with 0b0nnn -- n selects which TX buffer(s)
#define MCP_READ_STATUS 0xA0
#define MCP_BIT_MODIFY  0x05


// Register addresses, Table 11-2, p.63
#define MCP_CANSTAT     0x0E
#define MCP_CANCTRL     0x0F
#define MCP_CNF3        0x28
#define MCP_CNF2        0x29
#define MCP_CNF1        0x2A
#define MCP_CANINTE     0x2B   // which events pull INT low   (Register 7-1, p.52)
#define MCP_CANINTF     0x2C   // which events have happened  (Register 7-2, p.53)
#define MCP_RXB0CTRL    0x60   // receive buffer 0 control (Register 4-1, p.27)
#define MCP_RXB1CTRL    0x70   // receive buffer 1 control (Register 4-2, p.28)
#define MCP_RXM_ANY     0x60   // RXM = 11: masks/filters off, receive any message

// CANINTE / CANINTF bits (same position in both registers), p.52-53
#define MCP_RX0I        0x01   // message received in RXB0

// Operation modes, CANCTRL.REQOP / CANSTAT.OPMOD (upper 3 bits), p.60-61
#define MCP_MODE_MASK     0xE0
#define MCP_MODE_NORMAL   0x00
#define MCP_MODE_LOOPBACK 0x40
#define MCP_MODE_CONFIG   0x80   // set the configuration mode either CNF1 CNF2 CNF3

#define INT_CAN_DDR   DDRE
#define INT_CAN_PORT  PORTE
#define INT_CAN_PINR  PINE     // input register, used to read the pin level
#define INT_CAN_PIN   PE0  // INT

void mcp2515_int_init(void);
uint8_t mcp2515_init(uint8_t mode);
void mcp2515_reset(void);
void mcp2515_write(uint8_t address, uint8_t data);
void mcp2515_rts(uint8_t buffer_mask);
uint8_t mcp2515_read(uint8_t address);
uint8_t mcp2515_read_status(void);
void mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data);

uint8_t mcp2515_int_pending(void);

#endif //CAN2515_H_
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

void mcp2515_reset(void);
void mcp2515_write(uint8_t address, uint8_t data);
void mcp2515_rts(uint8_t buffer_mask);
uint8_t mcp2515_read_status(void);
void mcp2515_bit_modify(uint8_t address, uint8_t mask, uint8_t data):
#endif //CAN2515_H_
/*
 * can.h
 *
 * Created: 01.10.2026 23:19:13
 *  Author: ngabo
 */ 
/*
 * CAN message layer on top of the MCP2515 driver (Can2515.c).
 * Can2515.c knows about SPI instructions and registers.
 * This file knows about CAN MESSAGES: an ID, a length and up to 8 data bytes.
 */
 
#ifndef CAN_H_
#define CAN_H_
 
#include <stdint.h>
 
/*
* One CAN message (standard data frame, datasheet section 2.1, p.7):
*   id     -- 11-bit identifier (0x000 - 0x7FF). Lower id = higher priority.
*   length -- number of data bytes, 0..8 (the "DLC")
*
* The 8 data bytes can be used in two ways -- both names share the SAME
* 8 bytes of memory (that is what a union does):
*   data[0..7]      -- as raw bytes (what can_send()/can_receive() use)
*   positions[0..1] -- as two signed 32-bit numbers, e.g. joystick x and y
*
*   bytes:      [0][1][2][3] [4][5][6][7]
*   data:        0  1  2  3   4  5  6  7
*   positions:  |----[0]----| |----[1]----|
*
* Using positions[] only works if the receiving node stores numbers the
* same way: 32-bit, lowest byte first (little-endian). AVR and ARM
* Cortex-M both do. uint8_t / int32_t are used instead of char / long
* so the sizes  are the same on every compiler.
*/
typedef struct {
	uint16_t id;
	uint8_t  length;
	union {
		uint8_t data[8];        // raw bytes
		int32_t positions[2];   // same 8 bytes as two 32-bit numbers
	};
} can_message_t;
 
void    can_send(const can_message_t *msg);
uint8_t can_receive(can_message_t *msg);   // returns 1 if a message was read, 0 if none
 void can_loopback_test(void);
uint8_t can_wait_receive(can_message_t *msg, uint8_t timeout_ms);
 
#endif //CAN_H_
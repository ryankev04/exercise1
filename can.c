/*
 * can.c
 *
 * Created: 01.10.2026 23:18:54
 *  Author: ngabo
 */

/*
 * Sending and receiving CAN messages through the MCP2515.
 * Only transmit buffer 0 (TXB0) and receive buffer 0 (RXB0) are used,
 * which is enough to get communication working.
 *
 * Datasheet: MCP2515 DS21801G
 *   Transmit registers : Register 3-1 .. 3-8, p.18-21
 *   Receive registers  : Register 4-4 .. 4-9, p.29-31
 *   CANINTF            : Register 7-2, p.53
 *   READ STATUS bits   : Figure 12-8, p.69
 */
 
 #include "usart.h"
#include "can.h"
#include "Can2515.h"
#include<util/delay.h>

// Transmit buffer 0 registers (p.18-21)
#define TXB0CTRL  0x30
#define TXB0SIDH  0x31
#define TXB0SIDL  0x32
#define TXB0DLC   0x35
#define TXB0D0    0x36   // data byte 0; bytes 1..7 follow at 0x37..0x3D

// Receive buffer 0 registers (p.29-31)
#define RXB0SIDH  0x61
#define RXB0SIDL  0x62
#define RXB0DLC   0x65
#define RXB0D0    0x66   // data byte 0; bytes 1..7 follow at 0x67..0x6D

// Interrupt flag register (p.53)
#define CANINTF   0x2C
#define RX0IF     0x01   // bit 0: a message has arrived in RXB0

// READ STATUS result bits (Figure 12-8, p.69)
#define STATUS_RX0IF    0x01   // bit 0: message waiting in RXB0
#define STATUS_TX0REQ   0x04   // bit 2: TXB0 still waiting to be sent


/*
* can_send(): puts one message into transmit buffer 0 and tells the
* MCP2515 to send it.
*
* Steps (section 3.1 and 3.3, p.15):
*   1. Wait until TXB0 is free (TXREQ = 0). Writing into a buffer that is
*      still being sent is not allowed (note in section 3.1).
*   2. Write the ID      -> TXB0SIDH / TXB0SIDL
*   3. Write the length  -> TXB0DLC
*   4. Write the data    -> TXB0D0 .. TXB0D7
*   5. Send RTS (request-to-send) for buffer 0.
*/
void can_send(const can_message_t *msg)
{
	
	//  Wait for the previous message in TXB0 to finish.
	while (mcp2515_read_status() & STATUS_TX0REQ) { }
	
	//  The 11-bit ID is split over two registers (Register 3-3 and 3-4):
	//    SIDH holds ID bits 10..3  -> id >> 3
	//    SIDL holds ID bits  2..0 in its top 3 bits (7..5) -> (id & 0x07) << 5
	//    EXIDE (bit 3 of SIDL) = 0 -> standard 11-bit ID, not extended.
	mcp2515_write(TXB0SIDH, (uint8_t)(msg->id >> 3));
	mcp2515_write(TXB0SIDL, (uint8_t)((msg->id & 0x07) << 5));
	
	//  Length: lower 4 bits of TXB0DLC (Register 3-7). RTR bit = 0 -> data frame.
	uint8_t length = msg->length;
	if (length > 8) 
	{
		length = 8;
	}
	mcp2515_write(TXB0DLC, length);
	
	//  Data bytes, one register each (Register 3-8).
	for (uint8_t i = 0; i < length; i++) 
	{
		mcp2515_write(TXB0D0 + i, msg->data[i]); // put the data bytes ine by one
	}
	
	//  Request-to-send for TXB0 only (bit 0 of the mask), section 12.7.
	mcp2515_rts(0x01);
}

uint8_t can_receive(can_message_t *msg)
{
		// Anything in RXB0?
		if (!(mcp2515_read_status() & STATUS_RX0IF)) {
			return 0;
		}
		
		// ID: reverse of the split done in can_send() (Register 4-4 and 4-5).
		// READ the ID and paste it to the register 
		uint8_t sidh = mcp2515_read(RXB0SIDH);
		uint8_t sidl = mcp2515_read(RXB0SIDL);
		msg->id = ((uint16_t)sidh << 3) | (sidl >> 5);
		
		// Length: lower 4 bits of RXB0DLC (Register 4-8).
		msg->length = mcp2515_read(RXB0DLC) & 0x0F;
		if (msg->length > 8) 
		{
			msg->length = 8;
		}
		
		// Data bytes (Register 4-9).
		for (uint8_t i = 0; i < msg->length; i++)
		 {
			msg->data[i] = mcp2515_read(RXB0D0 + i); // copy the data bytes out
		}
		
		// Clear RX0IF so RXB0 can take the next message (section 4.1.3, p.23).
		// Bit Modify is the recommended way to clear CANINTF flags (section 7.0, p.51).
		mcp2515_bit_modify(CANINTF, RX0IF, 0x00);
		
		return 1;
}



/*
* can_wait_receive(): waits up to ~timeout_ms for the MCP2515 to signal a
* new message on its INT pin (PE0/INT2), then reads it.
* Only talks SPI when the interrupt says there is something to read.
* Returns 1 if a message was read, 0 on timeout.
*/
 uint8_t can_wait_receive(can_message_t *msg, uint8_t timeout_ms)
{
	for (uint8_t t = 0; t < timeout_ms; t++) {
		if (mcp2515_int_pending() && can_receive(msg)) {
			return 1;
		}
		_delay_ms(1);
	}
	return 0;
}

/*
* can_loopback_test(): sends two messages in Loopback mode and prints what
* comes back. Output goes to RS232, so call it BEFORE stdout is switched
* to the OLED.
*   Test 1: raw bytes  (data[])      -> expect  48 69 21  ("Hi!")
*   Test 2: numbers    (positions[]) -> expect  1500 and -300
*/
 void can_loopback_test(void)
{
	// ---- Test 1: raw bytes ----------------------------------------------
	can_message_t tx1 = {
		.id = 0x123,
		.length = 3,
		.data = { 'H', 'i', '!' }
	};
	can_send(&tx1);
	printf("Sent     id=0x%03X len=%d\n", tx1.id, tx1.length);
	
	can_message_t rx1;
	if (can_wait_receive(&rx1, 100)) {
		printf("Received id=0x%03X len=%d data:", rx1.id, rx1.length);
		for (uint8_t i = 0; i < rx1.length; i++) {
			printf(" %c", rx1.data[i]);
		}
		printf("\n");
		} else {
		printf("Test 1: nothing received\n");
	}
	
	// ---- Test 2: two 32-bit numbers through the union -------------------
	can_message_t tx2 = { .id = 0x042, .length = 8 };   // 8 bytes = both numbers
	tx2.positions[0] = 1500;
	tx2.positions[1] = -300;
	can_send(&tx2);
	printf("Sent     id=0x%03X pos0=%ld pos1=%ld\n", tx2.id, tx2.positions[0], tx2.positions[1]);
	
	can_message_t rx2;
	if (can_wait_receive(&rx2, 100)) {
		printf("Received id=0x%03X pos0=%ld pos1=%ld\n", rx2.id, rx2.positions[0], rx2.positions[1]);
		} else {
		printf("Test 2: nothing received\n");
	}
}
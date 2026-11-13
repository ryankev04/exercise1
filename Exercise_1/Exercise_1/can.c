/*
 * can.c
 *
 * Created: 01.10.2026 23:18:54
 *  Author: ngabo
 */

/*
 * Sending and receiving CAN messages through the MCP2515.
 * This message layer uses transmit buffer 0 (TXB0) and reads receive
 * buffer 0 (RXB0). Can2515.c handles the underlying SPI commands.
 *
 * Datasheet: MCP2515 DS21801G
 *   Transmit registers : Register 3-1 .. 3-8, p.18-21
 *   Receive registers  : Register 4-4 .. 4-9, p.29-31
 *   CANINTF            : Register 7-2, p.53
 *   READ STATUS bits   : Figure 12-8, p.69
 */
 
#include "usart.h"
#include "Io.h" 
#include "can.h"
#include "Can2515.h"
#include "Menu.h"
#include<util/delay.h>
#include <avr/pgmspace.h> // printf_P / PSTR: keep the text in flash, not RAM

// TXB0 holds the outgoing frame. These constants are SPI register addresses.
#define TXB0CTRL  0x30 // Control and status: TXREQ (bit 3), priority, and transmit errors.
#define TXB0SIDH  0x31 // Standard Identifier High: ID bits 10..3.
#define TXB0SIDL  0x32 // Standard Identifier Low: ID bits 2..0 in bits 7..5; EXIDE selects extended IDs.
#define TXB0DLC   0x35 // Data Length Code in bits 3..0; RTR (bit 6) selects a remote frame.
#define TXB0D0    0x36 // First payload register; add the byte index (0..7) to reach D0..D7.

// RXB0 holds the received frame. These constants are SPI register addresses.
#define RXB0SIDH  0x61 // Standard Identifier High: received ID bits 10..3.
#define RXB0SIDL  0x62 // Standard Identifier Low: ID bits 2..0 in bits 7..5; IDE marks an extended ID.
#define RXB0DLC   0x65 // Received Data Length Code in bits 3..0.
#define RXB0D0    0x66 // First received payload register; add the byte index (0..7) to reach D0..D7.

// CANINTF is a register address. RX0IF is the mask used with Bit Modify.
#define CANINTF   0x2C // CAN Interrupt Flags register.
#define RX0IF     0x01 // CANINTF bit 0: RXB0 is full. Clear it after copying the frame.

// READ STATUS returns a separate byte with its own bit layout (datasheet Fig. 12-8).
#define STATUS_RX0IF    0x01 // READ STATUS bit 0 mirrors CANINTF.RX0IF.
#define STATUS_TX0REQ   0x04 // READ STATUS bit 2 mirrors TXB0CTRL.TXREQ (register bit 3).


uint8_t  can_send(const can_message_t *msg){
	// A pending request means the controller still owns the contents of TXB0.
	if (mcp2515_read_status() & STATUS_TX0REQ) return 1;
	
	mcp2515_write(TXB0SIDH, (uint8_t)(msg->id >> 3));
	mcp2515_write(TXB0SIDL, (uint8_t)((msg->id & 0x07) << 5));
	
	uint8_t length = msg->length;
	if (length > 8) {
		length = 8;
	}
	mcp2515_write(TXB0DLC, length);
	
	for (uint8_t i = 0; i < length; i++) {
		mcp2515_write(TXB0D0 + i, msg->data[i]);
	}
	
	// In the RTS command mask, bit 0 selects buffer 0. It is a buffer selector.
	mcp2515_rts(0x01);
	
	return 0;
}
/************************************************************************/
/*
 * can_send(): Loads a standard CAN data frame into TXB0 and requests
 * transmission. If TXB0 is already pending (TXREQ is set), return 1
 * immediately and leave the pending frame alone.
 *
 * SIDH holds identifier bits 10..3. SIDL bits 7..5 hold identifier
 * bits 2..0, with EXIDE clear for an 11-bit identifier. The data
 * length is limited to eight bytes and writing TXB0DLC leaves RTR
 * clear. The payload goes into consecutive data registers.
 *
 * RTS mask 0x01 selects TXB0. Return 0 once the request is issued,
 * before the controller reports the result of transmission.
 *
 * Reference: MCP2515 datasheet, pp. 15, 18-21, 68-69.
 */
/************************************************************************/


void can_send_joystick(uint8_t x, uint8_t y, uint8_t button_joy, nav_event_t nav, uint8_t button_id_right,
                       uint8_t game_cmd, uint8_t control_mode){
	can_message_t msg = {
		.id = CAN_ID_JOYSTICK,
		.length = 7,
		.data = {x, y, button_joy, nav, button_id_right, game_cmd, control_mode}
	};
	can_send(&msg);
}
/************************************************************************/
/*
 * can_send_joystick(): Packs Node 1 input and game state into the
 * seven-byte CAN_ID_JOYSTICK frame for Node 2.
 *
 * Byte order is X, Y, joystick button, navigation event, right
 * button, game command, and control mode. The function makes one
 * call to can_send(). If TXB0 is busy, this sample is skipped and
 * the caller can submit a fresh sample on a later cycle.
 */
/************************************************************************/


uint8_t can_receive(can_message_t *msg){
	if (!(mcp2515_read_status() & STATUS_RX0IF)) {
		return 0;
	}
		
	uint8_t sidh = mcp2515_read(RXB0SIDH);
	uint8_t sidl = mcp2515_read(RXB0SIDL);
	msg->id = ((uint16_t)sidh << 3) | (sidl >> 5);
		
	// Keep only the DLC bits. The data array has room for eight bytes.
	msg->length = mcp2515_read(RXB0DLC) & 0x0F;
	if (msg->length > 8) {
		msg->length = 8;
	}
		
	for (uint8_t i = 0; i < msg->length; i++){
		msg->data[i] = mcp2515_read(RXB0D0 + i);
	}
		
	mcp2515_bit_modify(CANINTF, RX0IF, 0x00);
		
	return 1;
}
/************************************************************************/
/*
 * can_receive(): Copies a pending frame from RXB0 into *msg.
 * Returns 0 if RX0IF is clear, leaving *msg untouched.
 *
 * Reassembles the 11 standard ID bits from SIDH and SIDL, reads
 * the low four DLC bits, and copies up to eight payload bytes.
 * The decoding assumes a standard data frame.
 *
 * After copying, Bit Modify clears just RX0IF in CANINTF, freeing
 * RXB0 for the next frame. Returns 1 once the copy is complete.
 *
 * Reference: MCP2515 datasheet, pp. 23, 29-31, 51, 53, 69.
 */
/************************************************************************/


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
/************************************************************************/
/*
 * can_wait_receive(): Checks for an RXB0 message once per loop,
 * with a 1 ms delay after each unsuccessful check.
 *
 * mcp2515_int_pending() checks the saved interrupt flag and the
 * active-low INT pin. Only a pending indication triggers the SPI
 * receive check. Returns 1 if a message is read, or 0 after
 * timeout_ms iterations. A zero timeout performs no checks.
 * The elapsed wait also includes the time spent checking.
 */
/************************************************************************/


void can_test(void){
	can_message_t tx1 = { 
		.id = 0x123, 
		.length = 3,
		 .data = { 'H', 'i', '!' } };
	// Zero confirms that the driver issued RTS. The following register
	// reads show what happened in the controller after that request.
	if (can_send(&tx1) == 0) {
		printf_P(PSTR("Sent id: 0x%03X, len=%d\n"), tx1.id, tx1.length);
		_delay_ms(10);
		printf_P(PSTR("TXB0CTRL=%02X CANINTF=%02X TEC=%u REC=%u EFLG=%02X\n"), mcp2515_read(0x30), mcp2515_read(0x2C), mcp2515_read(0x1C), mcp2515_read(0x1D), mcp2515_read(0x2D));
	} else {
		printf_P(PSTR("Send failed: TXB0 stuck (no ACK from Node 2)\n"));
	}
}
/************************************************************************/
/*
 * can_test(): Requests transmission of "Hi!" as a standard
 * three-byte frame with ID 0x123 and prints diagnostic output.
 *
 * The first print reports that the transmit request was issued.
 * After 10 ms the function prints TXB0CTRL and CANINTF, the
 * transmit and receive error counters (TEC and REC), and EFLG.
 * These register values are a snapshot of the controller state.
 *
 * The failure branch means TXB0 already had a pending request.
 * A missing ACK is one possible cause. The return from can_send()
 * does not identify the cause or establish delivery.
 *
 * Reference: MCP2515 datasheet, pp. 15, 18, 49-50, 53.
 */
/************************************************************************/


/************************************************************************/
/*
misc:
 *
 * can_loopback_test(): sends two messages in Loopback mode and prints what
 * comes back. Output goes to RS232, so call it BEFORE stdout is switched
 * to the OLED.
 *   Test 1: raw bytes  (data[])      -> expect  48 69 21  ("Hi!")
 *   Test 2: numbers    (positions[]) -> expect  1500 and -300
 *
 * void can_loopback_test(void)
 * {
 * 	// ---- Test 1: raw bytes ----------------------------------------------
 * 	can_message_t tx1 = {
 * 		.id = 0x123,
 * 		.length = 3,
 * 		.data = { 'H', 'i', '!' }
 * 	};
 * 	can_send(&tx1);
 * 	printf("Sent     id=0x%03X len=%d\n", tx1.id, tx1.length);
 *
 * 	can_message_t rx1;
 * 	if (can_wait_receive(&rx1, 100)) {
 * 		printf("Received id=0x%03X len=%d data:", rx1.id, rx1.length);
 * 		for (uint8_t i = 0; i < rx1.length; i++) {
 * 			printf(" %c", rx1.data[i]);
 * 		}
 * 		printf("\n");
 * 		} else {
 * 		printf("Test 1: nothing received\n");
 * 	}
 *
 * 	// ---- Test 2: two 32-bit numbers through the union ----------
 * 	can_message_t tx2 = { .id = 0x042, .length = 8 };   // 8 bytes = both numbers
 * 	tx2.positions[0] = 1500;
 * 	tx2.positions[1] = -300;
 * 	can_send(&tx2);
 * 	printf("Sent     id=0x%03X pos0=%ld pos1=%ld\n", tx2.id, tx2.positions[0], tx2.positions[1]);
 *
 * 	can_message_t rx2;
 * 	if (can_wait_receive(&rx2, 100)) {
 * 		printf("Received id=0x%03X pos0=%ld pos1=%ld\n", rx2.id, rx2.positions[0], rx2.positions[1]);
 * 		} else {
 * 		printf("Test 2: nothing received\n");
 * 	}
 * }
 *
 * Node 2's if statement is not the problem. Node 2 mailbox 1 never becomes MRDY. Only after MRDY=1 does "can_rx entered" get printed.
 */
/************************************************************************/

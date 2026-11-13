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
#include "Menu.h"
 
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
		uint8_t data[8];      // raw bytes
		int32_t positions[2]; // same 8 bytes as two 32-bit numbers
	}; // union does this: if msg.data[0] = 1, ...[1] = 0, ...[2] = 0, ...[3] = 0, msg.positions[0] = 0x00000001.
} can_message_t;
 
// CAN IDs (must be the same on Node 2)
//   CAN_ID_JOYSTICK (0x100) is defined in Io.h: Node 1 -> Node 2
#define CAN_ID_STATUS   0x200 // Node 2 -> Node 1: motor status + game stats

// Game command, byte 5 of the joystick message (must match Node 2's can.h)
#define GAME_CMD_STOP   0 // in the menu: motor off
#define GAME_CMD_PLAY   1 // playing (Node 2 calibrates the first time)
#define GAME_CMD_PAUSE  2 // paused: motor off, calibration kept

/* Sound (Options -> Sound). Node 2 plays the music: SD card -> DAC -> speaker.
 * These numbers must be the same as in sound.h on Node 2.                  */
#define CAN_ID_SOUND       0x110 // Node 1 -> Node 2: [0] automatic music enabled (0 off, 1 on), [1] master volume 0..10 (0 muted)
#define CAN_ID_SOUND_INFO  0x210 // Node 2 -> Node 1: [0] songs on the card, [1] song playing, [2] state
#define CAN_ID_SOUND_NAME  0x211 // Node 2 -> Node 1: [0] song number, [1] part 0..3, [2..7] 6 letters
#define SOUND_NAME_PARTS   4     // the name comes in 4 parts of 6 letters
#define SOUND_NAME_MAX     20    // letters of a name (the OLED row has 21)

#define SOUND_STATE_OK         0 // state byte of CAN_ID_SOUND_INFO
#define SOUND_STATE_NO_CARD    1
#define SOUND_STATE_NOT_FAT32  2
#define SOUND_STATE_NO_SONGS   3
#define SOUND_STATE_BAD_FILE   4
#define SOUND_STATE_READ_ERR   5

uint8_t can_send(const can_message_t *msg);
/* Joystick message: original seven fields plus packed audio state/event byte.
 * See audio_protocol.h. Motor/game command bytes are unchanged. */
void can_send_joystick(uint8_t x, uint8_t y, uint8_t button_joy, nav_event_t nav, uint8_t button_id_right,
                       uint8_t game_cmd, uint8_t control_mode, uint8_t audio);
uint8_t can_receive(can_message_t *msg); // returns 1 if a message was read, 0 if none
void can_test(void);
uint8_t can_wait_receive(can_message_t *msg, uint8_t timeout_ms);

 
#endif // CAN_H_

/************************************************************************/
/*
misc:
 *
 * void can_loopback_test(void);
 */
/************************************************************************/

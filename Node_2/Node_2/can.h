
#ifndef CAN_H_
#define CAN_H_
#pragma once

#include <stdint.h>

#define CAN_ID_JOYSTICK 0x100   // Node 1 -> Node 2: joystick, buttons, game command
#define CAN_ID_STATUS   0x200   // Node 2 -> Node 1: motor status + game stats

/* Joystick message from Node 1 (CAN_ID_JOYSTICK), 7 bytes:
 *   byte 0: joystick X        byte 1: joystick Y
 *   byte 2: joystick button   byte 3: nav event
 *   byte 4: right buttons (bit 4 = SR5 = shoot)
 *   byte 5: game command (GAME_CMD_...)
 *   byte 6: control mode  (0 = position, 1 = speed)                        */
#define GAME_CMD_STOP   0       // in the menu: motor off
#define GAME_CMD_PLAY   1       // playing (starts calibration the first time)
#define GAME_CMD_PAUSE  2       // paused: motor off, calibration kept

/* Status message from Node 2 (CAN_ID_STATUS), 7 bytes:
 *   byte 0: status (PI_STATUS_... in PIController.h)
 *   byte 1: goals (ball through the IR beam) this game
 *   byte 2-3: shots this game (low byte first)
 *   byte 4-5: play time in seconds (low byte first)
 *   byte 6: control mode Node 2 is using                                     */

// Struct with bit timing information
// See `can_init` for usage example
typedef struct CanInit CanInit;
__attribute__((packed)) struct CanInit {
    union {
        struct {
            uint32_t phase2:4;  // Phase 2 segment
            uint32_t propag:4;  // Propagation time segment
            uint32_t phase1:4;  // Phase 1 segment
            uint32_t sjw:4;     // Synchronization jump width
            uint32_t brp:8;     // Baud rate prescaler
            uint32_t smp:8;     // Sampling mode
        };
        uint32_t reg;
    };
};


// Initialize CAN bus, with bit timings and optional interrupt
// If `rxInterrupt` is not 0, an interrupt will be triggered when a message is received.
// (See can.c for an example interrupt handler)
// Example:
//    can_init((CanInit){.brp = F_CPU/2000000-1, .phase1 = 5, .phase2 = 1, .propag = 6}, 0);
void can_init(CanInit init, uint8_t rxInterrupt);


// Strict-aliasing-safe reinterpret-cast
#define union_cast(type, x) \
    (((union { \
        typeof(x) a; \
        type b; \
    })x).b)


// Dummy type for use with `union_cast`, see below
typedef struct Byte8 Byte8;
struct Byte8 {
    uint8_t bytes[8];
};


// CAN message data type
// Data fields have 3 access methods (via union):
//  8 bytes
//  2 double words (32-bit ints)
//  1 Byte8 dummy struct
// The dummy struct allows for convenient construction of a CAN message from another type
//
// Example:
//    typedef struct {
//        uint16_t  a;
//        uint8_t   b;
//        float     c;
//    } __attribute__((packed)) YourStruct ;
//    
//    CanMsg m = (CanMsg){
//        .id = 1,
//        .length = sizeof(YourStruct),
//        .byte8 = union_cast(Byte8, ((YourStruct){
//            .a = 10,
//            .b = 20,
//            .c = -30.0,
//        })),
//    };
//    can_printmsg(m);
//    // Should print: CanMsg(id:1, length:7, data:{10, 0, 20, 0, 0, 240, 193})
typedef struct CanMsg CanMsg;
struct CanMsg {
    uint16_t id;
    uint8_t length;
    union {
        uint8_t     byte[8];
        uint32_t    dword[2];
        Byte8       byte8;
    };    
};

// Send a CAN message on the bus. 
// Blocks if the bus does not receive the message (typically because one of the 
// receiving nodes has not cleared a buffer)
void can_tx(CanMsg m);

// Send a CAN message WITHOUT waiting. Returns 0 (message skipped) if the
// transmit mailbox is still busy, 1 if it was queued.
uint8_t can_tx_try(CanMsg m);

// Receive a CAN message.
// Does not block. Returns 0 if there is no message, 1 otherwise
uint8_t can_rx(CanMsg* m);

// Print a CAN message (using `printf`)
void can_printmsg(CanMsg m);



  #endif //CAN_H_


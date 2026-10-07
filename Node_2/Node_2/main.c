/*
 * Node_2.c
 *
 * Created: 02.10.2026 13:47:57
 * Author : ngabo
 */ 

#include <stdio.h>
#include <stdarg.h>
#include "sam.h"
#include "uart.h"
#include "can.h"
#include "menu.h"



int main(void)
{
    /* Initialize the SAM system */
    SystemInit();
	WDT->WDT_MR = WDT_MR_WDDIS;        // disable watchdog, 
	

	uart_init(F_CPU, BAUD);
	printf("Hello  from ATSAMx\n\r");
	printf("TEST 1 BUILD %s %s\n\r", __DATE__, __TIME__);
		
	// 500 kbit/s: TQ = 21/84 MHz = 250 ns, Sync 1 + Prop 3 + PS1 2 + PS2 2 = 8 TQ
	CanInit init = { .reg = CAN_BR_BRP(20) | CAN_BR_SJW(0) |CAN_BR_PROPAG(2) | CAN_BR_PHASE1(1) | CAN_BR_PHASE2(1) };
	can_init(init, 0);
	printf("CAN_BR = 0x%08lX\n\r", (unsigned long)CAN0->CAN_BR);
    /* Replace with your application code */
	static uint8_t counting_loop = 0;
	while (1) {
		CanMsg m;
		if (can_rx(&m)) {
			if (m.id == CAN_ID_JOYSTICK && m.length >= 4){
				uint8_t x = m.byte[0];
				uint8_t y = m.byte[1];
				uint8_t button = m.byte[2];
				nav_event_t joystick_nav = m.byte[3];

				if (counting_loop >= 10) {
					printf("Joystick: X=%u Y=%u BTN: %u, NAV: %u\n\r", x, y, button, joystick_nav); 
					counting_loop = 0;
				}
				else counting_loop++;
			}
		}
	}
}

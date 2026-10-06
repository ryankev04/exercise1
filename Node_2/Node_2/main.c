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




int main(void)
{
    /* Initialize the SAM system */
    SystemInit();
	WDT->WDT_MR = WDT_MR_WDDIS;        // disable watchdog, 
	

	    uart_init(F_CPU, BAUD);
	    printf("Hello  from ATSAMx\n\r");
		
		// 500 kbit/s: TQ = 21/84 MHz = 250 ns, Sync 1 + Prop 3 + PS1 2 + PS2 2 = 8 TQ
		CanInit init = { .reg = CAN_BR_BRP(20) | CAN_BR_SJW(0) |CAN_BR_PROPAG(2) | CAN_BR_PHASE1(1) | CAN_BR_PHASE2(1) };
		can_init(init, 0);
		printf("CAN_BR = 0x%08lX\n\r", (unsigned long)CAN0->CAN_BR);
    /* Replace with your application code */
 CanMsg m;
 while (1)
 {
	 if (can_rx(&m)) {
		 printf(" ready to receive\n");
		 can_printmsg(m);
	 }
 }
}

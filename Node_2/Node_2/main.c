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
#include "pwm.h"
#include "motor.h"
#include "IR.h"
#include "adc.h"




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
	
	servo_init();
	printf("Servo: CPRD=%u  min=%u  centre=%u  max=%u\n\r",SERVO_CPRD, SERVO_CDTY_MIN, SERVO_CDTY_CENTER, SERVO_CDTY_MAX);
	motor_init();
		
	// starting the ADC 
	adc_init();
	timer_init();
	encoder_init();
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
				
				motor_set_from_joystick(x); //motor
				servo_set_from_joystick(y); //servo


				if (counting_loop >= 10) {
                    printf("X=%u Y=%u BTN=%u NAV=%u  enc=%ld  CDTY=%lu\r\n", x, y, button, joystick_nav, (long)encoder_read(),(unsigned long)servo_get_cdty());
					//IR_count_goal();

					counting_loop = 0;
				}
				else
				{ 
					counting_loop++;
				}
			}
		}
						if (IR_goal_happened())
						{
							printf("GOAL!  score = %u\r\n", IR_get_score());
						}
	
	}
}

/*
 * Node_2.c
 *
 * Created: 02.10.2026 13:47:57
 * Author : ngabo
 */ 

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include "sam.h"
#include "uart.h"
#include "can.h"
#include "menu.h"
#include "pwm.h"
#include "motor.h"
#include "IR.h"
#include "adc.h"
#include "timer.h"
#include "Encoder.h"
#include "PIController.h"
#include "Solenoid.h"




int main(void)
{
    /* Initialize the SAM system */
	//motor_safe_off(); //Doesn't help.
    SystemInit();
	WDT->WDT_MR = WDT_MR_WDDIS;        // disable watchdog, 
	
	static uint8_t previous_sr5 = 0;
	
	
	uart_init(F_CPU, BAUD);
	printf("Hello  from ATSAMx\n\r");
	printf("Exercise 8 Test BUILD %s %s\n\r", __DATE__, __TIME__);
		
	// 500 kbit/s: TQ = 21/84 MHz = 250 ns, Sync 1 + Prop 3 + PS1 2 + PS2 2 = 8 TQ
	CanInit init = { .reg = CAN_BR_BRP(20) | CAN_BR_SJW(0) |CAN_BR_PROPAG(2) | CAN_BR_PHASE1(1) | CAN_BR_PHASE2(1) };
	can_init(init, 0);
	printf("CAN_BR = 0x%08lX\n\r", (unsigned long)CAN0->CAN_BR);
	
	servo_init();
	printf("Servo: CPRD=%u  min=%u  centre=%u  max=%u\n\r",SERVO_CPRD, SERVO_CDTY_MIN, SERVO_CDTY_CENTER, SERVO_CDTY_MAX);
	motor_init();
		
	// starting the ADC 
	adc_init();
	encoder_init(); 
	PIControl_init(); //PIControl_print calls encoder_read();
	Solenoid_init();
	timer_init(); //TC0_Handler calls Solenoid_update() and PIControl_tick();
	
	PIControl_command('C'); //Start automatic calibration once at startup.  
	printf("PIController controls:\nS: STOP\nP: Print values\nOnly when the PI Controller is stopped:\n  C: Calibrate\n  R: Reverse Polarity\n"
	  "V: Verified\n  ',' and '.' to test the motor left and right.\n  A: Activate.\n");
	uint32_t printed_ms = timer_millis(); //To keep track of time.
 	static uint8_t counting_loop_CANRXprint = 0;
	while (1) {
		CanMsg m;
		if (can_rx(&m)) {
			if (m.id == CAN_ID_JOYSTICK && m.length >= 5){
				uint8_t x = m.byte[0];
				uint8_t y = m.byte[1];
				uint8_t button_joy = m.byte[2];
				nav_event_t joystick_nav = m.byte[3];
				uint8_t button_id_right = m.byte[4];
				
				bool SR5_state = (button_id_right & (1u << 4)) != 0u;
				if (SR5_state && !previous_sr5) {
					Solenoid_run(timer_millis());
				}
				previous_sr5 = SR5_state;
				
// 				motor_set_from_joystick(x); //motorV1
				PIControl_joystick(x, timer_millis()); //motorV2
				servo_set_from_joystick(y); //servo

				if (counting_loop_CANRXprint >= 10) {
					printf("X=%u ; Y=%u ; BTN_JOY=%u ; NAV=%u ; BTN_ID:%u ; enc=%ld ; CDTY=%lu\r\n", x, y, button_joy, joystick_nav, button_id_right, (long)encoder_read(),(unsigned long)servo_get_cdty());
					//IR_count_goal();
					counting_loop_CANRXprint = 0;
				}
				else{ 
					counting_loop_CANRXprint++;
				}
			}	
		}
		uint8_t command;
		if (uart_rx(&command)) PIControl_command(command);
		if (IR_goal_happened()) printf("GOAL!  score = %u\r\n", IR_get_score());
		uint32_t now_ms = timer_millis();
		if ((uint32_t)(now_ms - printed_ms) >= 1000u) {printed_ms = now_ms; PIControl_print();} //Execute every second. 
		
	}
}

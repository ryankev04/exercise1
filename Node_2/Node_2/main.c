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
#include "audio.h"
#include "sound.h" // music from the SD card -> DAC0 -> amplifier




/*
 * DEBUG_PRINT
 *   1 = print joystick values and PIControl_print() to the terminal (testing)
 *   0 = no prints while playing (printf blocks the main loop)
 */
#define DEBUG_PRINT  0 // 0 for music: each print at 9600 baud blocks the loop ~50 ms

#define STATUS_PERIOD_MS  200u // send the status message to Node 1 5 times per second

int main(void)
{
    /*
     * Initialize the SAM system
     */
    SystemInit();
	WDT->WDT_MR = WDT_MR_WDDIS; // disable watchdog,
	
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
	PIControl_init(); // PIControl_print calls encoder_read();
	Solenoid_init();
	timer_init(); // TC0_Handler calls Solenoid_update() and PIControl_tick();
	audio_init(1);
	sound_init(); // after timer_init(): the SD card start-up uses timer_millis() for timeouts
	
	/*
	 * No calibration at start-up any more: it starts when "Ping Pong" is
	 * selected on Node 1 (game command STOP -> PLAY). The terminal commands
	 * (C, V, A, S ...) still work for testing.
	 */
	printf("Waiting for Node 1: select Games -> Ping Pong\n\r");

	/*
	 * ---- Game state, controlled by Node 1 --------------------------------
	 */
	uint8_t  game_cmd_prev = GAME_CMD_STOP; // last game command from Node 1
	uint8_t  game_cmd      = GAME_CMD_STOP;
	uint8_t  control_mode  = 0; // 0 = position, 1 = speed
	uint16_t shots         = 0; // solenoid shots this game
	uint16_t goals_start   = 0; // IR score when the game started
	uint32_t play_ms       = 0; // time spent actually playing

	uint32_t printed_ms = timer_millis(); // To keep track of time.
	uint32_t status_ms  = timer_millis();
	uint32_t last_ms    = timer_millis();
#if DEBUG_PRINT
 	static uint8_t counting_loop_CANRXprint = 0;
#endif

	while (1) {
		CanMsg m;
		if (can_rx(&m)) {
			if (m.id == CAN_ID_JOYSTICK && m.length >= 7){
				if (m.length >= 8) {
					static uint8_t audio_started;
					if (!audio_started) {
						audio_init(timer_millis());
						audio_started = 1;
					}
					audio_snapshot(m.byte[7]);
				}
				uint8_t x = m.byte[0];
				uint8_t y = m.byte[1];
				uint8_t button_joy = m.byte[2];
				nav_event_t joystick_nav = m.byte[3];
				uint8_t button_id_right = m.byte[4];
				game_cmd     = m.byte[5];
				control_mode = m.byte[6];

				/*
				 * ---- Did Node 1 change the game state? ------------------
				 *   STOP  -> PLAY : new game -> calibrate, then play
				 *   PAUSE -> PLAY : resume (no new calibration)
				 *   any   -> PAUSE: motor off, keep calibration
				 *   any   -> STOP : motor off (back to the menu)
				 */
				if (game_cmd != game_cmd_prev) {
					if (game_cmd == GAME_CMD_PLAY && game_cmd_prev == GAME_CMD_STOP) {
						PIControl_start_game(control_mode);
						servo_enable(1);
						shots = 0;
						play_ms = 0;
						goals_start = IR_get_score();
						audio_goal(0);
						printf("Node 1: START game (mode %u)\n\r", control_mode);
					}
					else if (game_cmd == GAME_CMD_PLAY) {
						PIControl_resume(control_mode);
						servo_enable(1);
						printf("Node 1: RESUME\n\r");
					}
					else if (game_cmd == GAME_CMD_PAUSE) {
						PIControl_pause();
						servo_enable(0);
						printf("Node 1: PAUSE\n\r");
					}
					else {
						PIControl_stop_game();
						servo_enable(0);
						printf("Node 1: STOP game\n\r");
					}
					game_cmd_prev = game_cmd;
				}

				// Always give the controller the newest joystick value
				// (this also keeps its CAN timeout happy).
				PIControl_joystick(x, timer_millis());

				/*
				 * Servo and solenoid only while the game is really running.
				 */
				uint8_t st = PIControl_status();
				uint8_t playing = (game_cmd == GAME_CMD_PLAY) &&
				                  (st == PI_STATUS_POSITION || st == PI_STATUS_SPEED);

				bool SR5_state = (button_id_right & (1u << 4)) != 0u;
				if (playing) {
					servo_set_from_joystick(y); // servo
					if (SR5_state && !previous_sr5) {
						Solenoid_run(timer_millis());
						shots++;
					}
				}
				previous_sr5 = SR5_state;

#if DEBUG_PRINT
				if (counting_loop_CANRXprint >= 10) {
					printf("X=%u Y=%u CMD=%u ST=%u CDTY=%lu enc=%ld\r\n",
					x, y, game_cmd, PIControl_status(),
					(unsigned long)servo_get_cdty(), (long)encoder_read());
					counting_loop_CANRXprint = 0;
				}
				else{ 
					counting_loop_CANRXprint++;
				}
#else
				(void)button_joy; (void)joystick_nav;
#endif
			}
			/*
			 * Sound choice from Node 1 (Options -> Sound): song number + volume
			 */
			else if (m.id == CAN_ID_SOUND && m.length >= 2) {
				sound_set(m.byte[0], m.byte[1]);
			}
		}

		uint8_t command;
		if (uart_rx(&command)) {
			if (command == '?') {
				sound_report();
			} else {
				PIControl_command(command);
			}
		}
		if (IR_goal_happened()) printf("GOAL!  score = %u\r\n", IR_get_score());

		audio_goal((uint16_t)(IR_get_score() - goals_start));
		uint32_t now_ms = timer_millis();

		/*
		 * ---- Play time: only counts while the motor is really playing ----
		 */
		uint8_t st = PIControl_status();
		if (game_cmd == GAME_CMD_PLAY && (st == PI_STATUS_POSITION || st == PI_STATUS_SPEED)) {
			play_ms += (uint32_t)(now_ms - last_ms);
		}
		last_ms = now_ms;

		/*
		 * ---- Status message to Node 1, 5 times per second ----------------
		 */
		if ((uint32_t)(now_ms - status_ms) >= STATUS_PERIOD_MS) {
			status_ms = now_ms;
			uint16_t goals = IR_get_score() - goals_start;
			uint16_t secs  = (uint16_t)(play_ms / 1000u);
			CanMsg s = {
				.id = CAN_ID_STATUS,
				.length = 7,
				.byte = { st, (uint8_t)goals,
				          (uint8_t)shots, (uint8_t)(shots >> 8),
				          (uint8_t)secs,  (uint8_t)(secs >> 8),
				          control_mode }
			};
			can_tx_try(s); // never waits, even if Node 1 is off
		}

		/*
		 * ---- Music: refill the sound buffer from the SD card, and tell ----
		 * ---- Node 1 the song list / name (every 50 ms)               ----
		 */
		sound_update();
		sound_can_update(now_ms);

#if DEBUG_PRINT
		if ((uint32_t)(now_ms - printed_ms) >= 1000u) {printed_ms = now_ms; PIControl_print();} // Execute every second.
#else
		(void)printed_ms;
#endif
	}
}

/************************************************************************/
/*
misc:
 *
 * printf("X=%u ; Y=%u ; BTN_JOY=%u ; NAV=%u ; BTN_ID:%u ; CMD=%u ; MODE=%u ; enc=%ld\r\n", x, y, button_joy,
 * joystick_nav, button_id_right, game_cmd, control_mode, (long)encoder_read());
 */
/************************************************************************/

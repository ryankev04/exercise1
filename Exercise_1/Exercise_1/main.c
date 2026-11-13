/*
 * TTK4155_Exercise1_V1.c
 *
 * Created: 02/09/2026 09:24:38
 * Author : muval, ngabo
 */ 



/************************************************************************/
/*
 * ========================================== INCLUDES
 */
/************************************************************************/

#include "usart.h"
#include "Sram.h"
#include "Decoder.h"
#include "ADC.h"
#include "Spi.h"
#include "Can2515.h"
#include "can.h"
#include "oled.h"
#include "Io.h"
#include "Menu.h"
#include "car.h" // Turbo Rush game
#include "hof.h" // High Scores (top 3 per game in EEPROM)
#include <util/delay.h>
#include <avr/pgmspace.h> // PSTR(): text kept in flash, not RAM


/* Status codes from Node 2 (byte 0 of the status message, PIController.h) */
#define N2_IDLE          0 // Node 2 reports idle.
#define N2_CALIBRATING   1 // Node 2 reports calibration in progress.
#define N2_VERIFYING     2 // Node 2 reports motor verification.
#define N2_POSITION      3 // Node 2 reports position mode.
#define N2_SPEED         4 // Node 2 reports speed mode.
#define N2_READY         5 // Protocol code for ready.
#define N2_FAILED        6 // Node 2 reports startup failure.

/*
 * CAN_SELFTEST
 *   1 = send a test message at start-up and print the result (debugging)
 *   0 = omit the startup test call
 */
#define CAN_SELFTEST  0 // Compile in the startup CAN test when nonzero.

#define SR6_MASK  0x20 // right button 6: pause / cancel / back
#define SR5_MASK  0x10 // right button 5: shoot (Ping Pong) / boost (Turbo Rush)
#define PP_LIVES  3    // Ping Pong: game over after this many goals

/*
 * These thresholds count loop passes. Elapsed time also depends on the
 * work done in each pass.
 */
#define STALE_LOOPS           60 // Startup passes before checking failure or stale status.
#define NODE2_TIMEOUT_LOOPS  300 // Status is stale once its age exceeds this many passes.

typedef enum {
	GS_MENU,       // main menu, Games, Options (motor off)
	GS_ABOUT,      // About page
	GS_STARTING,   // waiting for Node 2: calibrating -> verifying -> arming
	GS_PLAYING,    // game running, joystick controls the motor
	GS_PAUSED,     // pause menu
	GS_STATS,      // statistics page (from the pause menu)
	GS_CAR,        // Turbo Rush running
	GS_CAR_PAUSED, // Turbo Rush paused
	GS_GAME_OVER,  // result screen (both games)
	GS_HOF_ENTRY,  // typing initials for a new high score
	GS_HOF_VIEW,   // High Scores table
	GS_SOUND       // Options -> Sound
} game_state_t;

static game_state_t gs = GS_MENU;
static uint8_t  control_mode = 0;         // 0 = position (PI), 1 = speed
static uint8_t  start_failed = 0;         // 1 = calibration failed / Node 2 not found
static uint8_t  start_stage  = 0xFF;      // checklist step shown on the STARTING screen
static uint8_t  start_bar    = 0;         // animated progress bar, 0..100 %
static uint16_t state_loops  = 0;         // Saturating loop count reset when Ping Pong startup begins.
static uint8_t  wait_neutral = 0;         // 1 = ignore the stick until it is centred again
static uint8_t  last_game    = HOF_TURBO; // which game the game-over screen belongs to
static uint8_t  hof_game     = HOF_TURBO; // which table the High Scores shows
static uint8_t  hof_from_menu = 0;        // 1 = High Scores opened from the main menu
static uint16_t loop_count   = 0;         // counts every pass (used as a random seed)

/* Newest values from Node 2's status message */
static uint8_t  n2_status = N2_IDLE;
static uint8_t  n2_goals  = 0;
static uint16_t n2_shots  = 0;
static uint16_t n2_secs   = 0;
static uint16_t n2_age    = 0xFFFF; // loop passes since the last status message
static uint8_t  n2_new    = 0;      // 1 = a new status message arrived this pass

static game_state_t sound_return = GS_MENU; // Options was opened from the main or pause menu

/************************************************************************/
/*
 *  Small screen helpers. No printf: see "Text without printf" in oled.c.
 */
/************************************************************************/

static void draw_value(uint8_t row, PGM_P label, uint16_t value, PGM_P unit)
{
	oled_pos(row, 0);
	oled_print_P(label);
	oled_print_number(value, 0);
	if (unit != NULL) {
		oled_print_P(unit);
	}
	oled_clear_rest();
}
/************************************************************************/
/*
 * draw_value(): Writes a flash label, a decimal value, and an optional flash unit.
 * Clears the rest of the OLED row.
 */
/************************************************************************/


static void draw_two_texts(uint8_t row, PGM_P first, PGM_P second)
{
	oled_pos(row, 0);
	oled_print_P(first);
	oled_print_P(second);
	oled_clear_rest();
}
/************************************************************************/
/*
 * draw_two_texts(): Writes two flash strings in sequence, then clears the rest of the row.
 */
/************************************************************************/


static PGM_P mode_name(void)
{
	if (control_mode == 1) {
		return PSTR("Speed");
	}
	return PSTR("Position");
}
/************************************************************************/
/*
 * mode_name(): Returns the flash label for the selected control mode.
 */
/************************************************************************/

/************************************************************************/
/*
 *  Screens
 */
/************************************************************************/


static void draw_start_checklist(uint8_t stage)
{
	// stage 0 = calibrating, 1 = verifying motor, 2 = arming, 3 = all done
	PGM_P names[3] = { PSTR("Calibrating"), PSTR("Verifying motor"), PSTR("Arming motor") };
	for (uint8_t i = 0; i < 3; i++) {
		PGM_P mark;
		if (i < stage) {
			mark = PSTR("[x] "); // done
		}
		else if (i == stage) {
			mark = PSTR("[>] "); // busy now
		}
		else {
			mark = PSTR("[ ] "); // still to do
		}
		draw_two_texts(2 + i, mark, names[i]);
	}
}
/************************************************************************/
/*
 * draw_start_checklist(): Draws three checklist rows and marks them relative to stage.
 * A stage of 3 marks all three rows complete.
 */
/************************************************************************/


static void draw_starting(void)
{
	oled_clear();
	oled_print_title_P(0, PSTR("Ping Pong"));
	draw_start_checklist(0);
	draw_two_texts(5, PSTR("Mode: "), mode_name());
	oled_progress_bar(6, 0);
	oled_print_line_P(7, PSTR("SR6: Cancel"));
}
/************************************************************************/
/*
 * draw_starting(): Draws the initial Ping Pong startup screen with an empty progress bar.
 */
/************************************************************************/


static void draw_start_failed(PGM_P reason)
{
	oled_clear();
	oled_print_title_P(0, PSTR("Ping Pong"));
	oled_print_centered_P(3, reason);
	oled_print_centered_P(4, PSTR("Check motor power"));
	oled_print_line_P(7, PSTR("SR6: Back to menu"));
}
/************************************************************************/
/*
 * draw_start_failed(): Displays the supplied startup error and the SR6 return prompt.
 */
/************************************************************************/


static void draw_playing(uint8_t full)
{
	if (full) {
		oled_clear();
		oled_print_title_P(0, PSTR("Ping Pong"));
		oled_print_line_P(7, PSTR("SR6:Pause  SR5:Shoot"));
	}
	draw_two_texts(2, PSTR("Mode:  "), mode_name());
	draw_value(3, PSTR("Time:  "), n2_secs, PSTR(" s"));
	draw_value(4, PSTR("Shots: "), n2_shots, NULL);

	// Lives left, shown as stars: "* * *"
	uint8_t lives = 0;
	if (n2_goals < PP_LIVES) {
		lives = PP_LIVES - n2_goals;
	}
	oled_pos(5, 0);
	oled_print_P(PSTR("Lives: "));
	for (uint8_t i = 0; i < lives; i++) {
		oled_print_P(PSTR("* "));
	}
	oled_clear_rest();
	if (n2_age > NODE2_TIMEOUT_LOOPS) {
		oled_print_line_P(6, PSTR("Node 2: no signal!"));
	}
	else if (n2_status == N2_POSITION || n2_status == N2_SPEED) {
		oled_print_line_P(6, PSTR("Motor: ON"));
	}
	else {
		oled_print_line_P(6, PSTR("Motor: STOPPED"));
	}
}
/************************************************************************/
/*
 * draw_playing(): Refreshes mode, time, shots, lives, and the motor status indicator.
 * A nonzero full argument also clears and draws the fixed screen elements.
 */
/************************************************************************/


static void draw_stats(uint8_t full)
{
	if (full) {
		oled_clear();
		oled_print_title_P(0, PSTR("Stats"));
		oled_print_line_P(7, PSTR("< Back"));
	}
	draw_two_texts(2, PSTR("Mode:  "), mode_name());
	draw_value(3, PSTR("Time:  "), n2_secs, PSTR(" s"));
	draw_value(4, PSTR("Shots: "), n2_shots, NULL);
	draw_value(5, PSTR("Goals: "), n2_goals, NULL);
}
/************************************************************************/
/*
 * draw_stats(): Shows the latest Ping Pong time, shots, and goals.
 * A nonzero full argument also redraws the title and footer.
 */
/************************************************************************/


static void draw_about(void)
{
	oled_clear();
	oled_print_title_P(0, PSTR("About"));
	oled_print_line_P(2, PSTR("TTK4155 Byggern"));
	oled_print_line_P(3, PSTR("Node 1: ATmega162"));
	oled_print_line_P(4, PSTR("Node 2: Arduino Due"));
	oled_print_line_P(5, PSTR("Motor: PI / speed"));
	oled_print_line_P(7, PSTR("< Back"));
}
/************************************************************************/
/*
 * draw_about(): Draws the project and board information shown by the About menu.
 */
/************************************************************************/

/************************************************************************/
/*
 *  Menu actions (called by the menu when an item is selected)
 */
/************************************************************************/
static void start_pingpong(void);
static void resume_game(void);
static void stop_game(void);
static void show_stats(void);
static void show_about(void);
static void toggle_control(void);
static void start_turbo(void);
static void show_hof(void);
static void show_sound(void);

/*
 * Menu structure:
 *   Main Menu
 *     Games   -> Ping Pong (intro, calibration, game) / Turbo Rush / Back
 *     High Scores (top 3 per game)
 *     Options -> Control: Position|Speed / Sound / Back
 *     About
 *   Paused (in a game)
 *     Resume / Stop game / Options / Stats
 *
 * submenus[i]: page to open (NULL = "Coming soon", MENU_BACK = go back)
 * actions[i] : function to run instead (NULL = none). Max 20 chars per item.
 */
static menu_t options_menu = {
	.title = "Options",
	.items = { "Control: Position", "Sound", "Back" },
	.item_count = 3,
	.submenus = { NULL, NULL, MENU_BACK },
	.actions  = { toggle_control, show_sound, NULL }
};

static menu_t games_menu = {
	.title = "Games",
	.items = { "Ping Pong", "Turbo Rush", "Back" },
	.item_count = 3,
	.submenus = { NULL, NULL, MENU_BACK },
	.actions  = { start_pingpong, start_turbo, NULL }
};

static menu_t main_menu = {
	.title = "Main Menu",
	.items = { "Games", "High Scores", "Options", "About" },
	.item_count = 4,
	.submenus = { &games_menu, NULL, &options_menu, NULL },
	.actions  = { NULL, show_hof, NULL, show_about }
};

static menu_t pause_menu = {
	.title = "Paused",
	.items = { "Resume", "Stop game", "Options", "Stats" },
	.item_count = 4,
	.submenus = { NULL, NULL, &options_menu, NULL },
	.actions  = { resume_game, stop_game, NULL, show_stats }
};


static void start_pingpong(void)
{
	oled_intro("PING PONG"); // big title + loading bar + flashes (~2 s)
	gs = GS_STARTING;
	state_loops  = 0;
	start_failed = 0;
	start_stage  = 0;
	start_bar    = 0;
	n2_status    = N2_IDLE; // forget old status from before this game
	draw_starting();
}
/************************************************************************/
/*
 * start_pingpong(): Shows the intro, resets the startup display state, and enters GS_STARTING.
 */
/************************************************************************/


static void resume_game(void)
{
	gs = GS_PLAYING;
	draw_playing(1);
}
/************************************************************************/
/*
 * resume_game(): Returns to Ping Pong play and redraws the full playing screen.
 */
/************************************************************************/


static void stop_game(void)
{
	gs = GS_MENU;
	menu_init(&main_menu); // back to the main menu
}
/************************************************************************/
/*
 * stop_game(): Returns to the root menu. The main loop derives the CAN command from the new state.
 */
/************************************************************************/


static void show_stats(void)
{
	gs = GS_STATS;
	draw_stats(1);
}
/************************************************************************/
/*
 * show_stats(): Enters the stats screen from the pause menu and draws it in full.
 */
/************************************************************************/


static void show_about(void)
{
	gs = GS_ABOUT;
	draw_about();
}
/************************************************************************/
/*
 * show_about(): Enters and draws the About screen.
 */
/************************************************************************/


static void toggle_control(void)
{
	if (control_mode == 0) {
		control_mode = 1;
		options_menu.items[0] = "Control: Speed";
	}
	else {
		control_mode = 0;
		options_menu.items[0] = "Control: Position";
	}
	menu_redraw();
}
/************************************************************************/
/*
 * toggle_control(): Switches the control mode and updates the corresponding Options label.
 */
/************************************************************************/


static void start_turbo(void)
{
	oled_intro("TURBO RUSH");
	gs = GS_CAR;
	car_start(loop_count | 1); // Use the running loop count and force a nonzero seed.
}
/************************************************************************/
/*
 * start_turbo(): Shows the intro and starts a local Turbo Rush run using an odd seed derived from loop_count.
 */
/************************************************************************/


static void show_hof(void)
{
	gs = GS_HOF_VIEW;
	hof_from_menu = 1;
	hof_draw(hof_game);
}
/************************************************************************/
/*
 * show_hof(): Opens the selected high-score table and records that it was opened from the menu.
 */
/************************************************************************/


static void game_over(uint8_t game, uint16_t score)
{
	last_game = game;
	hof_game  = game;
	if (hof_qualifies(game, score)) {
		gs = GS_HOF_ENTRY;
		hof_entry_start(game, score);
		return;
	}
	gs = GS_GAME_OVER;
	oled_clear();
	oled_print_title_P(0, PSTR("Game Over"));
	// Score in big letters. Ping Pong counts seconds, so add " s" there.
	char text[9];
	uint8_t length = oled_number_to_text(score, 0, text);
	if (game == HOF_PINGPONG) {
		text[length]     = ' ';
		text[length + 1] = 's';
		text[length + 2] = '\0';
	}
	oled_print_big_centered(2, text);
	draw_value(4, PSTR("Best: "), hof_best(game), NULL);
	oled_print_line_P(6, PSTR("SR5: Play again"));
	oled_print_line_P(7, PSTR("< Menu"));
}
/************************************************************************/
/*
 * game_over(): Opens initials entry for a qualifying score. Otherwise shows the result,
 * the saved best, and the replay and menu prompts.
 */
/************************************************************************/


static void play_again(void)
{
	if (last_game == HOF_TURBO) {
		start_turbo();
	}
	else {
		start_pingpong();
	}
}
/************************************************************************/
/*
 * play_again(): Starts a new run of the game recorded by game_over().
 */
/************************************************************************/


static void show_sound(void)
{
	sound_return = gs;
	gs = GS_SOUND;
	oled_clear();
	oled_print_title_P(0, PSTR("Sound"));
	oled_print_centered_P(3, PSTR("Coming Soon"));
	oled_print_line_P(7, PSTR("< Back / SR6"));
}
/************************************************************************/
/*
 * show_sound(): Shows the Sound placeholder and saves the state to restore on exit.
 */
/************************************************************************/


static void update_starting(uint8_t sr6_edge)
{
	if (sr6_edge) { // cancel (or leave the "failed" screen)
		gs = GS_MENU;
		menu_redraw(); // back to the Games menu
		return;
	}
	if (start_failed) {
		return; // keep showing the error until SR6
	}

	// Defer failure and status age checks during the startup grace period.
	if (state_loops > STALE_LOOPS) {
		if (n2_status == N2_FAILED) {
			start_failed = 1;
			draw_start_failed(PSTR("Calibration failed!"));
			return;
		}
		if (n2_age > NODE2_TIMEOUT_LOOPS) {
			start_failed = 1;
			draw_start_failed(PSTR("Node 2 not found"));
			return;
		}
	}

	// Which step is Node 2 at?  (stage, and how far the bar may go)
	uint8_t stage, bar_target;
	if (n2_status == N2_POSITION || n2_status == N2_SPEED) { stage = 3; bar_target = 100; }
	else if (n2_status == N2_VERIFYING)                      { stage = 1; bar_target = 70;  }
	else                                                     { stage = 0; bar_target = 35;  }

	if (stage != start_stage) {
		start_stage = stage;
		draw_start_checklist(stage);
	}

	// Animate the bar: one step per loop pass towards the target
	if (start_bar < bar_target) {
		start_bar++;
		oled_progress_bar(6, start_bar);
	}

	// Everything done and the bar is full -> GO!
	if (stage == 3 && start_bar >= 100) {
		oled_clear();
		oled_print_big_centered(3, "GO!");
		oled_flash();
		_delay_ms(400);
		gs = GS_PLAYING;
		draw_playing(1);
	}
}
/************************************************************************/
/*
 * update_starting(): Handles cancel and startup failures, then updates the checklist and progress bar.
 * Position or speed status allows the bar to reach 100 and play to begin.
 */
/************************************************************************/

/*
 * The UART as a stdio stream for printf(). FDEV_SETUP_STREAM builds it at
 * compile time using the UART put and get callbacks.
 */
static FILE uart_stdio = FDEV_SETUP_STREAM(uart_putchar, uart_getchar, _FDEV_SETUP_RW);


int main(void)
{
	uart1_init();
	FILE *uart_stream = &uart_stdio;
	stdout = uart_stream;

	puts_P(PSTR("Node 1 (ATmega162) started")); // -> RS232 (puts: no printf needed)

	spi_master_init();
	
	// ---- CAN: init in normal mode and self-test (output -> RS232) ----
	if (mcp2515_init(MCP_MODE_NORMAL) != 0) {
		puts_P(PSTR("MCP2515 init FAILED"));
	} else {
		puts_P(PSTR("MCP2515 OK"));
		mcp2515_int_init(); // MCP2515 INT -> PE0 (INT2)
#if CAN_SELFTEST
		can_test();
#endif
		_delay_ms(500);	
	}
	oled_init();
	oled_clear();
	hof_init(); // set up the High Scores in EEPROM (first start only)

	menu_init(&main_menu); // power on -> straight to the main menu

	uint8_t sr6_prev = 0;
	uint8_t sr5_prev = 0;
	nav_event_t event_prev = NAV_NEUTRAL;

	while (1){
		io_buttons_t button = io_read_buttons();
		io_joystick_t joystick = io_read_joystick();
		nav_event_t joystick_event = nav_from_joystick(joystick);

		/*
		 * ---- Edges: "just pressed" (was not pressed last pass) ----------
		 */
		uint8_t sr6 = (button.right & SR6_MASK) != 0;
		uint8_t sr6_edge = sr6 && !sr6_prev;
		sr6_prev = sr6;
		uint8_t back_edge = (joystick_event == NAV_LEFT || joystick_event == NAV_CLICK)
		                    && joystick_event != event_prev;
		nav_event_t nav_edge = NAV_NEUTRAL; // the event, only in the pass it starts
		if (joystick_event != event_prev) {
			nav_edge = joystick_event;
		}
		nav_event_t event_prev_frame = event_prev; // for the car game (lane change edge)
		event_prev = joystick_event;
		loop_count++;

		/*
		 * ---- Messages from Node 2 ----------------------------------------
		 */
		if (n2_age < 0xFFFF) {
			n2_age++; // one more pass without a status message
		}
		can_message_t rx;
		if (can_receive(&rx)) {
			if (rx.id == CAN_ID_STATUS && rx.length >= 7) {
				n2_status = rx.data[0];
				n2_goals  = rx.data[1];
				n2_shots  = rx.data[2] | ((uint16_t)rx.data[3] << 8);
				n2_secs   = rx.data[4] | ((uint16_t)rx.data[5] << 8);
				n2_age = 0;
				n2_new = 1;
			}
		}

		/*
		 * ---- What to do in each state ------------------------------------
		 */
		switch (gs) {
			case GS_MENU:
			case GS_PAUSED:
				// After pausing, the stick may still be held sideways: wait until
				// it is centred, otherwise the pause menu would select something.
				if (wait_neutral) {
					if (joystick_event == NAV_NEUTRAL) wait_neutral = 0;
				}
				else {
					menu_update(joystick_event, button);
				}
				break;

			case GS_ABOUT:
				if (back_edge || sr6_edge) {
					gs = GS_MENU;
					menu_redraw();
				}
				break;

			case GS_STARTING:
				update_starting(sr6_edge);
				break;

			case GS_PLAYING:
				if (sr6_edge) { // SR6 = pause
					gs = GS_PAUSED;
					menu_init(&pause_menu);
					wait_neutral = 1;
				}
				else if (n2_goals >= PP_LIVES) { // all lives lost -> game over
					oled_flash();
					oled_flash();
					game_over(HOF_PINGPONG, n2_secs); // score = seconds survived
				}
				else if (n2_new || n2_age == NODE2_TIMEOUT_LOOPS + 1) {
					draw_playing(0); // update the live values
				}
				break;

			case GS_STATS:
				if (back_edge || sr6_edge) {
					gs = GS_PAUSED;
					menu_redraw(); // back to the pause menu
				}
				else if (n2_new) {
					draw_stats(0);
				}
				break;

			case GS_CAR: {
				if (sr6_edge) { // SR6 = pause
					gs = GS_CAR_PAUSED;
					oled_clear();
					oled_print_title_P(0, PSTR("Turbo Rush"));
					oled_print_big_centered(3, "PAUSED");
					oled_print_line_P(6, PSTR("SR6: Resume"));
					oled_print_line_P(7, PSTR("< Quit"));
					break;
				}
				// One lane per joystick push (edge), boost while SR5 is held
				int8_t move = 0;
				if (joystick_event != event_prev_frame) {
					if (joystick_event == NAV_UP)   move = -1;
					if (joystick_event == NAV_DOWN) move = +1;
				}
				if (car_update(move, (button.right & SR5_MASK) != 0)) {
					car_crash_animation();
					game_over(HOF_TURBO, car_score());
				}
				break;
			}

			case GS_CAR_PAUSED:
				if (sr6_edge) {
					gs = GS_CAR;
					car_redraw();
				}
				else if (back_edge) {
					gs = GS_MENU;
					menu_redraw();
				}
				break;

			case GS_GAME_OVER:
				if ((button.right & SR5_MASK) && !sr5_prev) {
					play_again();
				}
				else if (back_edge || sr6_edge) {
					gs = GS_MENU;
					menu_init(&main_menu);
				}
				break;

			case GS_HOF_ENTRY:
				if (hof_entry_update(nav_edge)) { // Initials entry finished.
					gs = GS_HOF_VIEW;
					hof_from_menu = 0;
				}
				break;

			case GS_HOF_VIEW:
				if (nav_edge == NAV_UP || nav_edge == NAV_DOWN) {
					hof_game = !hof_game; // switch between the two games
					hof_draw(hof_game);
				}
				else if (back_edge || sr6_edge) {
					gs = GS_MENU;
					if (hof_from_menu) menu_redraw();
					else               menu_init(&main_menu);
				}
				break;

			case GS_SOUND:
				if (back_edge || sr6_edge) {
					gs = sound_return;
					wait_neutral = 1;
					menu_redraw();
				}
				break;
		}
		sr5_prev = (button.right & SR5_MASK) != 0;
		n2_new = 0;
		if (state_loops < 0xFFFF) state_loops++;

		/*
		 * ---- Tell Node 2 what to do (byte 5 of the joystick message) ----
		 */
		uint8_t game_cmd;
		if ((gs == GS_STARTING && !start_failed) || gs == GS_PLAYING) {
			game_cmd = GAME_CMD_PLAY;
		}
		else if (gs == GS_PAUSED || gs == GS_STATS) {
			game_cmd = GAME_CMD_PAUSE;
		}
		else if (gs == GS_SOUND && sound_return == GS_PAUSED) {
			game_cmd = GAME_CMD_PAUSE; // Sound opened from the pause menu: keep the game
		}
		else {
			game_cmd = GAME_CMD_STOP;
		}
		can_send_joystick(joystick.x, joystick.y, joystick.btn, joystick_event, button.right,
		                  game_cmd, control_mode);

		uint8_t left_active = ((button.left & IO_LEFT_BUTTON_MASK) != 0) || (joystick_event != NAV_NEUTRAL);
		uint8_t right_active = ((button.right & IO_RIGHT_BUTTON_MASK) != 0) || ((button.nav & IO_NAV_BUTTON_MASK) != 0);
		io_led_groups_update(left_active, right_active);

		_delay_ms(5);
	}
}
/************************************************************************/
/*
 * main(): Initializes the UART, SPI, CAN, OLED, high scores, and menu.
 * Each loop samples inputs, reads CAN status, updates the active screen or game,
 * sends the joystick frame with a state-dependent command, and updates the LED banks.
 */
/************************************************************************/

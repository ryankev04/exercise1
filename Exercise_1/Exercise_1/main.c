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
#include "audio_ui.h"
uint8_t audio_ui; // one retained event token, no filenames/buffers on Node 1
#include "hof.h"  // High Scores (top 3 per game in EEPROM)
#include <util/delay.h>
#include <avr/pgmspace.h> // PSTR(), strncpy_P(): text kept in flash, not RAM
#include <string.h>


/************************************************************************/
/*
 *  GAME FLOW (Node 1)
 *
 *   MENU --Games/Ping Pong--> STARTING --Node 2 ready--> PLAYING
 *    ^                          |  SR6 = cancel            |  SR6
 *    |                          v                          v
 *    +--------"Stop game"----------------------------- PAUSED (menu)
 *                                                      Resume / Stop game /
 *                                                      Options / Stats
 *
 *  Node 1 tells Node 2 what to do with byte 5 of the joystick message:
 *    MENU, ABOUT         -> GAME_CMD_STOP   (motor off)
 *    STARTING, PLAYING   -> GAME_CMD_PLAY   (Node 2 calibrates, then plays)
 *    PAUSED, STATS       -> GAME_CMD_PAUSE  (motor off, calibration kept)
 *  Node 2 answers 5 times per second with a status message (CAN_ID_STATUS):
 *  motor state (calibrating / verifying / playing ...), goals, shots, time.
 *
 *  Turbo Rush (Games menu) runs only on Node 1 (OLED game), Node 2 stays off.
 *
 *  Options -> Sound: enable automatic music, set the
 *  volume (0..10) and mute. Node 1 sends the choice (CAN_ID_SOUND) and
 *  Node 2 answers with the number of songs and the song name.
 *  Ping Pong ends after PP_LIVES goals (ball through the IR beam). Both games
 *  can put a score in the High Scores (EEPROM, top 3 with initials).
 *
 *  Text that is only shown on the screen is kept in FLASH with PSTR(...),
 *  because the ATmega162 only has 1 KB of RAM.
 */
/************************************************************************/

/* Status codes from Node 2 (byte 0 of the status message, PIController.h) */
#define N2_IDLE          0
#define N2_CALIBRATING   1
#define N2_VERIFYING     2
#define N2_POSITION      3
#define N2_SPEED         4
#define N2_READY         5
#define N2_FAILED        6

/*
 * CAN_SELFTEST
 *   1 = send a test message at start-up and print the result (debugging)
 *   0 = skip it: saves about 350 bytes of program memory (flash)
 */
#define CAN_SELFTEST  0

#define SR6_MASK  0x20 // right button 6: pause / cancel / back
#define SR5_MASK  0x10 // right button 5: shoot (Ping Pong) / boost (Turbo Rush)
#define PP_LIVES  3    // Ping Pong: game over after this many goals

/*
 * Node 1 has no millisecond timer, so time is counted in main-loop passes.
 * One pass is about 5-10 ms (5 ms delay + SPI traffic), so these are rough.
 */
#define STALE_LOOPS           60 // ignore Node 2 status for ~0.5 s after starting
#define NODE2_TIMEOUT_LOOPS  300 // ~2-3 s without a status message = Node 2 not found
#define SOUND_SEND_LOOPS      20 // send the sound settings again every ~0.1-0.2 s
#define SOUND_VOLUME_MAX      10

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
	GS_SOUND       // Options -> Sound: song, volume, mute
} game_state_t;

static game_state_t gs = GS_MENU;
static uint8_t  control_mode = 0;         // 0 = position (PI), 1 = speed
static uint8_t  start_failed = 0;         // 1 = calibration failed / Node 2 not found
static uint8_t  start_stage  = 0xFF;      // checklist step shown on the STARTING screen
static uint8_t  start_bar    = 0;         // animated progress bar, 0..100 %
static uint16_t state_loops  = 0;         // loop passes since entering the current state
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

/* Sound settings (Options -> Sound). Node 2 plays the music. */
static uint8_t  sound_song   = 1;           // 0 = music off, 1 = automatic game soundtrack
static uint8_t  sound_volume = 5;           // 0..10
static uint8_t  sound_muted  = 0;           // 1 = silent (the volume is kept for later)
static uint8_t  sound_row    = 0;           // selected row: 0 = song, 1 = volume, 2 = mute
static uint8_t  sound_send   = 1;           // 1 = send the settings to Node 2 as soon as possible
static uint8_t  sound_loops  = 0;           // loop passes since the settings were sent
static game_state_t sound_return = GS_MENU; // "back" goes here: main menu or pause menu

/* Newest sound information from Node 2 */
static uint8_t  n2_songs       = 0;          // number of songs on the SD card
static uint8_t  n2_sound_state = 0xFF;       // SOUND_STATE_..., 0xFF = nothing heard yet
static uint8_t  n2_name_song   = 0;          // the song that n2_name belongs to
static uint8_t  n2_name_parts  = 0;          // bit 0..3 = which of the 4 name parts arrived
static char     n2_name[SOUND_NAME_MAX + 1]; // OLED-visible name + '\0'
static uint8_t  n2_sound_new   = 0;          // 1 = something new to show on the Sound screen

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
 * One row "label value unit", e.g. draw_value(3, PSTR("Time:  "), 42, PSTR(" s"))
 * unit = NULL -> no unit after the number
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
 * One row made of two flash texts, e.g. "Mode:  " + "Speed"
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
 * STARTING: checklist of what Node 2 is doing + progress bar
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

static void draw_start_failed(PGM_P reason)
{
	oled_clear();
	oled_print_title_P(0, PSTR("Ping Pong"));
	oled_print_centered_P(3, reason);
	oled_print_centered_P(4, PSTR("Check motor power"));
	oled_print_line_P(7, PSTR("SR6: Back to menu"));
}

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
 * PLAYING: live values from Node 2. full = 1 also draws the fixed parts.
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
 * STATS: summary of this game (opened from the pause menu)
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
 *  Sound screen (Options -> Sound)
 *
 *    row 0      Sound
 *    row 2    >Song:   2 of 12      '>' = selected row (up/down)
 *    row 3     kubbi_firelight      song name, or what is wrong ("No SD card" ...)
 *    row 4     Volume: 5
 *    row 5     [#####     ]
 *    row 6     Mute:   Off
 *    row 7    </>:Change SR6:Back   left/right changes the selected row
 */
/************************************************************************/

static void draw_sound_mark(uint8_t screen_row, uint8_t row) {
	oled_pos(screen_row, 0);
	if (sound_row == row) {
		oled_print_P(PSTR(">"));
	} else {
		oled_print_P(PSTR(" "));
	}
}
/************************************************************************/
/*
 * Column 0 of a row: '>' if this row is selected, else a space
 */
/************************************************************************/

static PGM_P sound_problem_text(void) {
	if (n2_sound_state == 0xFF) {
		return PSTR("Waiting for Node 2");
	}
	if (n2_sound_state == SOUND_STATE_NO_CARD) {
		return PSTR("No SD card");
	}
	if (n2_sound_state == SOUND_STATE_NOT_FAT32) {
		return PSTR("Card is not FAT32");
	}
	if (n2_sound_state == SOUND_STATE_NO_SONGS) {
		return PSTR("No .WAV files");
	}
	if (n2_sound_state == SOUND_STATE_BAD_FILE) {
		return PSTR("Can't play this file");
	}
	if (n2_sound_state == SOUND_STATE_READ_ERR) {
		return PSTR("SD card read error");
	}
	return NULL;
}
/************************************************************************/
/*
 * Text for a problem reported by Node 2, or NULL when everything is OK
 */
/************************************************************************/

static void draw_sound(uint8_t full) {
	if (full) {
		oled_clear();
		oled_print_title_P(0, PSTR("Sound"));
		oled_print_line_P(7, PSTR("</>:Change SR6:Back"));
	}

	// Row 2: automatic music enabled, or music off
	draw_sound_mark(2, 0);
	oled_print_P(PSTR("Song:   "));
	if (sound_song == 0) {
		oled_print_P(PSTR("Off"));
	} else {
		oled_print_P(PSTR("Auto"));
	}
	oled_clear_rest();

	// Row 3: the song name (centred), or what is wrong
	PGM_P problem = sound_problem_text();
	if (problem != NULL) {
		oled_print_centered_P(3, problem);
	} else if (sound_song == 0) {
		oled_print_centered_P(3, PSTR("Music off"));
	} else if (n2_name_parts == 0x0F) {
		oled_clear_line(3);
		oled_print_centered(3, n2_name);
	} else {
		oled_print_centered_P(3, n2_name_song ? PSTR("...") : PSTR("No music"));
	}

	// Row 4 and 5: volume as a number and as a bar
	draw_sound_mark(4, 1);
	oled_print_P(PSTR("Volume: "));
	oled_print_number(sound_volume, 0);
	if (sound_muted) {
		oled_print_P(PSTR(" (muted)"));
	}
	oled_clear_rest();
	oled_progress_bar(5, sound_volume * 10);

	// Row 6: mute
	draw_sound_mark(6, 2);
	if (sound_muted) {
		oled_print_P(PSTR("Mute:   On"));
	} else {
		oled_print_P(PSTR("Mute:   Off"));
	}
	oled_clear_rest();
}
/************************************************************************/
/*
 * full = 1 also draws the parts that never change (title, help line)
 */
/************************************************************************/

/* ---------------------------------------------------------------------------
 *  Menu actions (called by the menu when an item is selected)
 * ------------------------------------------------------------------------- */
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
 *     Options -> Control: Position|Speed / Sound (song, volume, mute) / Back
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
 * Games -> Ping Pong: intro animation, then wait for Node 2 to calibrate
 */
/************************************************************************/

static void resume_game(void)
{
	gs = GS_PLAYING;
	draw_playing(1);
}

static void stop_game(void)
{
	audio_event(AUD_BACK);
	gs = GS_MENU;
	menu_init(&main_menu); // back to the main menu
}

static void show_stats(void)
{
	gs = GS_STATS;
	draw_stats(1);
}

static void show_about(void)
{
	gs = GS_ABOUT;
	draw_about();
}

static void toggle_control(void)
{
	audio_event(AUD_CHANGE);
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
 * Options -> Control: switch between position (PI) and speed control.
 * The item text changes, so the player sees what is selected.
 */
/************************************************************************/

static void start_turbo(void)
{
	oled_intro("TURBO RUSH");
	gs = GS_CAR;
	car_start(loop_count | 1); // time spent in the menu = random seed (never 0)
}
/************************************************************************/
/*
 * Games -> Turbo Rush: runs completely on Node 1
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
 * Main menu -> High Scores
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
 *  Game over (both games): new high score -> initials, else result screen
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
 *  Sound: screen input, messages from and to Node 2
 */
/************************************************************************/

static void show_sound(void) {
	sound_return = gs; // GS_MENU or GS_PAUSED: where "back" goes
	gs = GS_SOUND;
	sound_row = 0;
	draw_sound(1);
}
/************************************************************************/
/*
 * Options -> Sound (from the main menu or from the pause menu)
 */
/************************************************************************/

static void sound_change(uint8_t right) {
	uint8_t old_song = sound_song, old_volume = sound_volume, old_mute = sound_muted;
	if (sound_row == 0) {
		// Automatic soundtrack on/off; file selection belongs to Node 2.
		sound_song = !sound_song;
	} else if (sound_row == 1) {
		// Volume 0..10, stops at both ends
		if (right && sound_volume < SOUND_VOLUME_MAX) {
			sound_volume++;
		}
		if (!right && sound_volume > 0) {
			sound_volume--;
		}
	} else {
		// Mute: on <-> off
		sound_muted = !sound_muted;
	}
	if (old_song != sound_song || old_volume != sound_volume || old_mute != sound_muted) {
		audio_event(AUD_CHANGE);
	}
	sound_send = 1; // tell Node 2 in the next pass
}
/************************************************************************/
/*
 * Joystick right (right = 1) or left (right = 0) on the selected row
 */
/************************************************************************/

static void update_sound(nav_event_t nav_edge, uint8_t sr6_edge) {
	if (sr6_edge || nav_edge == NAV_CLICK) {
		audio_event(AUD_BACK);
		gs = sound_return; // back to Options (main or pause menu)
		wait_neutral = 1;  // a held button must not select a menu item
		menu_redraw();
		return;
	}
	if (nav_edge == NAV_UP) {
		audio_event(AUD_NAV);
		if (sound_row == 0) {
			sound_row = 2;
		} else {
			sound_row--;
		}
		draw_sound(0);
	} else if (nav_edge == NAV_DOWN) {
		audio_event(AUD_NAV);
		sound_row++;
		if (sound_row > 2) {
			sound_row = 0;
		}
		draw_sound(0);
	} else if (nav_edge == NAV_RIGHT || nav_edge == NAV_LEFT) {
		sound_change(nav_edge == NAV_RIGHT);
		draw_sound(0);
	} else if (n2_sound_new) {
		draw_sound(0); // new name or state from Node 2
	}
}

static void read_sound_info(const can_message_t *rx) {
	/*
	 * INFO is authoritative: NAME now identifies the automatic selection,
	 * not the Auto/Off setting. Reuse the existing name ID, no extra SRAM.
	 */
	if (rx->data[1] != n2_name_song) {
		n2_name_song = rx->data[1];
		n2_name_parts = 0;
		n2_sound_new = 1;
	}
	if (rx->data[0] != n2_songs || rx->data[2] != n2_sound_state) {
		n2_songs = rx->data[0];
		n2_sound_state = rx->data[2];
		n2_sound_new = 1;
	}
}
/************************************************************************/
/*
 * CAN_ID_SOUND_INFO. n2_sound_new = 1 only when something changed, so the
 * screen is not redrawn every time the message arrives (10 times a second).
 */
/************************************************************************/

static void read_sound_name(const can_message_t *rx) {
	uint8_t song = rx->data[0];
	uint8_t part = rx->data[1];
	if (part >= SOUND_NAME_PARTS) {
		return;
	}
	uint8_t part_bit = (uint8_t)(1 << part); // part 0 -> bit 0 ... part 3 -> bit 3
	if (!song || song != n2_name_song) {
		return; // await matching INFO
	}
	if (n2_name_parts & part_bit) {
		return; // we already have this part
	}
	for (uint8_t i = 0; i < 6; i++) {
		uint8_t position = (uint8_t)(part * 6 + i);
		if (position < SOUND_NAME_MAX) {
			n2_name[position] = (char)rx->data[2 + i];
		}
	}
	n2_name[SOUND_NAME_MAX] = '\0'; // never longer than one OLED row
	n2_name_parts = n2_name_parts | part_bit;
	if (n2_name_parts == 0x0F) {
		n2_sound_new = 1; // name complete -> show it
	}
}
/************************************************************************/
/*
 * CAN_ID_SOUND_NAME: [0] song number, [1] part 0..3, [2..7] 6 letters.
 * The name is shown when all 4 parts of the same song have arrived.
 */
/************************************************************************/

static uint8_t send_sound_settings(void) {
	can_message_t msg;
	msg.id = CAN_ID_SOUND;
	msg.length = 2;
	msg.data[0] = sound_song;
	msg.data[1] = sound_volume;
	if (sound_muted) {
		msg.data[1] = 0;
	}
	return can_send(&msg) == 0; // can_send(): 0 = queued, 1 = busy
}
/************************************************************************/
/*
 * Song + volume to Node 2. Muted = volume 0 for Node 2.
 * Returns 1 if the message was queued, 0 if transmit buffer 0 was busy.
 */
/************************************************************************/

static void update_starting(uint8_t sr6_edge)
{
	if (sr6_edge) { // cancel (or leave the "failed" screen)
		audio_event(AUD_BACK);
		gs = GS_MENU;
		menu_redraw(); // back to the Games menu
		return;
	}
	if (start_failed) {
		return; // keep showing the error until SR6
	}

	// Errors (only after STALE_LOOPS, so an old status from before can't trigger them)
	if (state_loops > STALE_LOOPS) {
		if (n2_status == N2_FAILED) {
			start_failed = 1;
			audio_event(AUD_ERROR);
			draw_start_failed(PSTR("Calibration failed!"));
			return;
		}
		if (n2_age > NODE2_TIMEOUT_LOOPS) {
			start_failed = 1;
			audio_event(AUD_ERROR);
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
 *  STARTING: follow Node 2 through calibrating -> verifying -> arming
 */
/************************************************************************/

/*
 * The UART as a stdio stream for printf(). FDEV_SETUP_STREAM builds it at
 * compile time; fdevopen() would do the same at run time but pulls in
 * malloc()/free(), which costs about 700 bytes of flash we need for the games.
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
			else if (rx.id == CAN_ID_SOUND_INFO && rx.length >= 3) {
				read_sound_info(&rx);
			}
			else if (rx.id == CAN_ID_SOUND_NAME && rx.length == 8) {
				read_sound_name(&rx);
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
					audio_event(AUD_BACK);
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
					audio_event(AUD_BACK);
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
					audio_event(AUD_LOST);
					/*
					 * Best-effort immediate audio before the blocking animation.
					 * Turbo already commands motor STOP. Retained token retries later.
					 */
					can_send_joystick(joystick.x, joystick.y, joystick.btn, joystick_event,
					                  button.right, GAME_CMD_STOP, control_mode, audio_ui | AUD_OVER);
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
					audio_event(AUD_BACK);
					gs = GS_MENU;
					menu_redraw();
				}
				break;

			case GS_GAME_OVER:
				if ((button.right & SR5_MASK) && !sr5_prev) {
					play_again();
				}
				else if (back_edge || sr6_edge) {
					audio_event(AUD_BACK);
					gs = GS_MENU;
					menu_init(&main_menu);
				}
				break;

			case GS_HOF_ENTRY:
				if (hof_entry_update(nav_edge)) { // name saved -> show the table
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
					audio_event(AUD_BACK);
					gs = GS_MENU;
					if (hof_from_menu) menu_redraw();
					else               menu_init(&main_menu);
				}
				break;

			case GS_SOUND:
				update_sound(nav_edge, sr6_edge);
				break;
		}
		sr5_prev = (button.right & SR5_MASK) != 0;
		n2_new = 0;
		n2_sound_new = 0;
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
		/*
		 * Sound context does not alter the physical motor command above.
		 */
		uint8_t audio_state = AUD_MENU;
		if (gs == GS_PLAYING) {
			audio_state = AUD_BLUE;
		} else if (gs == GS_PAUSED || gs == GS_STATS ||
		           (gs == GS_SOUND && sound_return == GS_PAUSED)) {
			audio_state = AUD_BLUE_PAUSE;
		} else if (gs == GS_CAR) {
			audio_state = AUD_TURBO;
		} else if (gs == GS_CAR_PAUSED) {
			audio_state = AUD_TURBO_PAUSE;
		} else if (gs == GS_GAME_OVER || gs == GS_HOF_ENTRY ||
		           (gs == GS_HOF_VIEW && !hof_from_menu)) {
			audio_state = AUD_OVER;
		}
		/*
		 * Normally the joystick message. Every SOUND_SEND_LOOPS passes, and
		 * right after a change on the Sound screen, the sound settings go
		 * instead (both use transmit buffer 0, so one message per pass).
		 * Sending them again and again means Node 2 also gets them when it
		 * is switched on after Node 1.
		 */
		sound_loops++;
		if (sound_loops >= SOUND_SEND_LOOPS) {
			sound_send = 1;
		}
		if (sound_send) {
			if (send_sound_settings()) {
				sound_send  = 0;
				sound_loops = 0;
			}
		}
		else {
			can_send_joystick(joystick.x, joystick.y, joystick.btn, joystick_event, button.right,
			                  game_cmd, control_mode, audio_ui | audio_state);
		}

		uint8_t left_active = ((button.left & IO_LEFT_BUTTON_MASK) != 0) || (joystick_event != NAV_NEUTRAL);
		uint8_t right_active = ((button.right & IO_RIGHT_BUTTON_MASK) != 0) || ((button.nav & IO_NAV_BUTTON_MASK) != 0);
		io_led_groups_update(left_active, right_active);

		_delay_ms(5);
	}
}

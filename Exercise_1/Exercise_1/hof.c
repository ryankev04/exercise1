/*
 * hof.c - HIGH SCORES (see hof.h)
 *
 * ---------------------------------------------------------------------------
 * EEPROM LAYOUT (ATmega162 has 512 bytes of EEPROM)
 * ---------------------------------------------------------------------------
 *   ee_magic            1 byte   0x5A = table has been set up
 *   ee_table[2][3]      2 games x 3 entries x 5 bytes = 30 bytes
 *       entry = 3 letters + uint16_t score
 *
 * EEMEM tells the compiler to place a variable in EEPROM. It can NOT be read
 * like a normal variable: use eeprom_read_block() / eeprom_update_block().
 * "update" only writes bytes that changed, which saves EEPROM wear
 * (each EEPROM byte survives about 100 000 writes).
 *
 * A chip erase also erases EEPROM unless the EESAVE fuse is programmed.
 * ---------------------------------------------------------------------------
 */
#include "hof.h"
#include "oled.h"
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <string.h>

#define HOF_MAGIC 0x5A // EEPROM marker for initialized score tables.

typedef struct {
	char     name[3]; // Exactly three initials, stored without a terminator.
	uint16_t score; // Points or elapsed seconds, depending on the game.
} hof_entry_t;

static uint8_t     EEMEM ee_magic; // Initialization marker in EEPROM.
static hof_entry_t EEMEM ee_table[HOF_GAMES][HOF_SIZE]; // One ranked table per game.

static void read_entry(uint8_t game, uint8_t i, hof_entry_t *e)
{
	eeprom_read_block(e, &ee_table[game][i], sizeof(hof_entry_t));
}
/************************************************************************/
/*
 * read_entry(): Copies one game's ranked entry from EEPROM into
 * the caller's RAM buffer. game and i must be valid table indices.
 */
/************************************************************************/

static void write_entry(uint8_t game, uint8_t i, const hof_entry_t *e)
{
	eeprom_update_block(e, &ee_table[game][i], sizeof(hof_entry_t));
}
/************************************************************************/
/*
 * write_entry(): Stores one RAM entry at the selected EEPROM
 * slot. eeprom_update_block() skips bytes that already match.
 */
/************************************************************************/

void hof_init(void)
{
	// First start (or after the EEPROM was erased): fill the table with "---  0"
	if (eeprom_read_byte(&ee_magic) != HOF_MAGIC) {
		hof_entry_t empty = { { '-', '-', '-' }, 0 };
		for (uint8_t g = 0; g < HOF_GAMES; g++) {
			for (uint8_t i = 0; i < HOF_SIZE; i++) {
				write_entry(g, i, &empty);
			}
		}
		eeprom_update_byte(&ee_magic, HOF_MAGIC);
	}
}
/************************************************************************/
/*
 * hof_init(): Initializes both score tables if the EEPROM marker
 * does not match HOF_MAGIC. Fills every slot with three dashes and
 * a zero score, then writes the marker after the table is filled.
 */
/************************************************************************/

uint16_t hof_best(uint8_t game)
{
	hof_entry_t e;
	read_entry(game, 0, &e);
	return e.score;
}
/************************************************************************/
/*
 * hof_best(): Returns the score stored in the first slot of the
 * selected game's table. A newly initialized table returns zero.
 */
/************************************************************************/

static uint8_t rank_of(uint8_t game, uint16_t score)
{
	hof_entry_t e;
	for (uint8_t i = 0; i < HOF_SIZE; i++) {
		read_entry(game, i, &e);
		if (score > e.score) return i;
	}
	return HOF_SIZE;
}
/************************************************************************/
/*
 * rank_of(): Returns the first rank whose score is strictly
 * lower than the candidate. Existing entries with equal scores
 * keep precedence. HOF_SIZE means there is no qualifying rank.
 */
/************************************************************************/

uint8_t hof_qualifies(uint8_t game, uint16_t score)
{
	return score > 0 && rank_of(game, score) < HOF_SIZE;
}
/************************************************************************/
/*
 * hof_qualifies(): Returns 1 if a positive score would fit in
 * the game's table. This check only reads EEPROM.
 */
/************************************************************************/

static uint8_t insert(uint8_t game, const char name[3], uint16_t score)
{
	uint8_t rank = rank_of(game, score);
	if (rank >= HOF_SIZE) return HOF_SIZE;

	hof_entry_t e;
	for (uint8_t i = HOF_SIZE - 1; i > rank; i--) { // shift down: 2 <- 1, 1 <- 0 ...
		read_entry(game, i - 1, &e);
		write_entry(game, i, &e);
	}
	memcpy(e.name, name, 3);
	e.score = score;
	write_entry(game, rank, &e);
	return rank;
}
/************************************************************************/
/*
 * insert(): Rechecks the rank, shifts lower entries down from
 * the bottom up, and writes the three initials and score at the
 * new rank. Returns that rank, or HOF_SIZE if the score cannot fit.
 */
/************************************************************************/

static PGM_P game_name(uint8_t game)
{
	if (game == HOF_TURBO) {
		return PSTR("Turbo Rush");
	}
	return PSTR("Ping Pong (sec)");
}
/************************************************************************/
/*
 * game_name(): Returns a flash string for the game's display
 * label. Turbo Rush has its own label, and the other branch
 * returns the Ping Pong label with its unit in seconds.
 */
/************************************************************************/

static char *from_flash(char *buf, PGM_P text)
{
	strncpy_P(buf, text, 21);
	buf[21] = '\0';
	return buf;
}
/************************************************************************/
/*
 * from_flash(): Copies up to 21 characters from program flash
 * into a RAM buffer of at least 22 bytes. It terminates the text
 * and returns buf for use with the OLED text functions.
 */
/************************************************************************/

static uint8_t highlight = 0xFF; // Rank to draw inverted. 0xFF means no highlight.

void hof_draw(uint8_t game)
{
	char buf[22];

	oled_clear();
	oled_print_title(0, from_flash(buf, PSTR("High Scores")));
	oled_print_centered(2, from_flash(buf, game_name(game)));

	for (uint8_t i = 0; i < HOF_SIZE; i++) {
		hof_entry_t e;
		read_entry(game, i, &e);

		// Build "1 ABC 12345" (11 characters = 88 pixels in the big 8x8 font)
		buf[0] = '1' + i; // place: 1, 2 or 3
		buf[1] = ' ';
		buf[2] = e.name[0];
		buf[3] = e.name[1];
		buf[4] = e.name[2];
		buf[5] = ' ';
		oled_number_to_text(e.score, 5, &buf[6]); // 5 wide, right-aligned
		oled_pos(3 + i, (128 - 8 * 11) / 2);
		if (i == highlight) oled_print_big_inverted(buf);
		else                oled_print_big(buf);
	}
	oled_print_line(7, from_flash(buf, PSTR("U/D: Game    < Back")));
}
/************************************************************************/
/*
 * hof_draw(): Draws one game's ranked scores on the OLED.
 * Each row shows the place, three initials, and a right-aligned
 * score. The row matching highlight is drawn inverted.
 */
/************************************************************************/

/************************************************************************/
/*
 *  Initials entry: up/down = letter, right/click = next letter, left = back
 */
/************************************************************************/
static uint8_t  entry_game; // Game whose table is being edited.
static uint16_t entry_score; // Score waiting for initials.
static uint8_t  entry_pos; // Index of the selected initial while editing.
static char     entry_name[3] = { 'A', 'A', 'A' }; // remembers the last name used

static void draw_entry(void)
{
	// Big letters, the one being edited is inverted (white box)
	oled_pos(5, (128 - 8 * 5) / 2); // "A B C" = 5 characters
	for (uint8_t i = 0; i < 3; i++) {
		char s[2] = { entry_name[i], '\0' };
		if (i == entry_pos) oled_print_big_inverted(s);
		else                oled_print_big(s);
		if (i < 2) oled_print_big(" ");
	}
	// "Letter 2 of 3" (13 characters, centred)
	oled_clear_line(6);
	oled_pos(6, (128 - 6 * 13) / 2);
	oled_print_P(PSTR("Letter "));
	oled_print_number(entry_pos + 1, 0);
	oled_print_P(PSTR(" of 3"));
}
/************************************************************************/
/*
 * draw_entry(): Redraws the three editable initials and the
 * letter counter. The currently selected initial is inverted.
 */
/************************************************************************/

void hof_entry_start(uint8_t game, uint16_t score)
{
	char buf[22];

	entry_game  = game;
	entry_score = score;
	entry_pos   = 0;

	oled_clear();
	oled_print_title(0, from_flash(buf, PSTR("NEW HIGH SCORE!")));
	oled_print_centered(1, from_flash(buf, game_name(game)));
	oled_print_big_number_centered(3, score);
	oled_print_line(7, from_flash(buf, PSTR("U/D:Letter  >:Next")));
	draw_entry();
	oled_flash();
}
/************************************************************************/
/*
 * hof_entry_start(): Sets up initials entry for the selected
 * game and score. Editing starts at the first letter with the
 * previous initials still in RAM. Draws the screen and flashes it.
 */
/************************************************************************/

uint8_t hof_entry_update(nav_event_t edge)
{
	char *c = &entry_name[entry_pos];

	if (edge == NAV_UP) { // next letter: A -> B ... Z -> A
		if (*c >= 'Z') {
			*c = 'A';
		}
		else {
			*c = *c + 1;
		}
	}
	else if (edge == NAV_DOWN) { // previous letter: B -> A ... A -> Z
		if (*c <= 'A') {
			*c = 'Z';
		}
		else {
			*c = *c - 1;
		}
	}
	else if (edge == NAV_LEFT) {
		if (entry_pos > 0) entry_pos--;
	}
	else if (edge == NAV_RIGHT || edge == NAV_CLICK) {
		entry_pos++;
		if (entry_pos >= 3) { // all 3 letters done -> save
			highlight = insert(entry_game, entry_name, entry_score);
			hof_draw(entry_game);
			highlight = 0xFF; // only highlight this one time
			return 1;
		}
	}
	else {
		return 0; // nothing pressed
	}
	draw_entry();
	return 0;
}
/************************************************************************/
/*
 * hof_entry_update(): Handles one navigation event. Up and down
 * cycle the selected letter through A-Z, left moves back, and
 * right or click advances. Advancing past the third letter tries
 * to save the score and draws the table with the new rank highlighted.
 *
 * Returns 1 when entry is finished, including when the insert
 * finds no qualifying rank. The caller then leaves entry mode.
 */
/************************************************************************/

/*
 * oled.c
 *
 * Created: 22.09.2026 10:43:19
 *  Author: ngabo
 */ 

#include "oled.h"
#include "Spi.h"
#include "fonts.h"
#include "usart.h"
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <string.h>     // strlen()

void oled_command(uint8_t cmd)
{
	
// &= bitwise AND  ~=bitwise NOT
// (1 << OLED_DC_PIN) =  a mask with a single 1 in bit position 2
// ~(....) flips it in this case it will be 11111011
//OLED_DC_PORT &= 11111011 = forces bit 2 to 0, keeps every other bit of PORTD exactly as it was
//So this line just for set D/C# to be zero
	OLED_DC_PORT &= ~(1 << OLED_DC_PIN);   // D/C#=0 -> command register
	spi_select_slave(SPI_SLAVE_DISPLAY);
	spi_transfer_byte(cmd);
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}

/*
* write_column: the column the next data byte will be written to.
* In page addressing mode the OLED moves one column to the right after
* every data byte, so we follow it here. oled_clear_rest() uses it to know
* where the text ended.
*/
static uint8_t write_column = 0;

void oled_data(uint8_t data)
{
	write_column++;
	OLED_DC_PORT |= (1 << OLED_DC_PIN);    // D/C#=1 -> GDDRAM
	spi_select_slave(SPI_SLAVE_DISPLAY);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}


/*
* Screen addressing: the display is split into 8 horizontal "pages"
* (each 8 pixels tall) x 128 columns. To draw anywhere, you must pick
* BOTH a page and a column before sending pixel data.
*/

void oled_goto_page(uint8_t page)
{
	// page & 0x07 keeps page in range 0-7 (only 3 bits matter)
	// 0xB0 | page combines with the base command -> B0h-B7h
	// 0xB0 = fixed set page prefix. Low 3 bits carry the page number
	// (3 bits is enough to count 0-7). "& 0x07" keeps it in range.
	oled_command(0xB0 | (page & 0x07));   // datasheet p.32
}

// Picks which column (0-127) to start writing at.
void oled_goto_column(uint8_t col)
{
	write_column = col;
	// Column needs 7 bits, so it's sent in two halves.
	// Column needs 7 bits (0-127), but each command only carries 4 
	// so the value is split into two halves and sent as two commands.
	oled_command(0x00 | (col & 0x0F));         // lower nibble, p.32
	oled_command(0x10 | ((col >> 4) & 0x0F));  // upper nibble, p.32
}

// Moves to the very top-left corner of the screen.
void oled_home(void)
{
	oled_goto_page(0);
	oled_goto_column(0);
}

// Moves to any page + column in one call, instead of two.
void oled_pos(uint8_t row, uint8_t column)
{
	oled_goto_page(row);
	oled_goto_column(column);
}
void oled_reset(void)
{
	// Datasheet p.26, Power ON sequence: RES# low >=3us (t1) triggers reset.
	OLED_RES_PORT |= (1 << OLED_RES_PIN);
	_delay_us(10);
	OLED_RES_PORT &= ~(1 << OLED_RES_PIN);
	_delay_us(10);
	OLED_RES_PORT |= (1 << OLED_RES_PIN);
	_delay_ms(10);
}


void oled_init(void)
{
	OLED_DC_DDR  |= (1 << OLED_DC_PIN);
	OLED_RES_DDR |= (1 << OLED_RES_PIN);

	oled_reset();
	
	// IO board doc own Recommended minimal initialization:
	//   A1 (segment remap), C8 (scan direction), AF (output enable)
	oled_command(0xAE);
	oled_command(0xD5); // clock divide ratio, divide ratio = A[3:0] + 1
	oled_command(0x80); // higher bit oscillator frequency setting, higher value = faster
	oled_command(0xA8); // "Set Multiplex Ratio", Table 9-4 p.33. MUX ratio = value + 1.
	oled_command(0x3F);	// 0x3F (63) -> 64 MUX, matching this panel's actual 64 physical rows.
	oled_command(0xD3); // "Set Display Offset", Table 9-4 p.33. Vertical shift of RAM vs
	oled_command(0x00); // physical rows. 0x00 = no shift.
	
	// Set Display Start Line, Table 9-4 p.33. Low 6 bits pick which RAM .
	//Row appears at the physical top of the screen. 0x40 = start line 0.
	// Orientation  IO board doc's own recommendation:
	// "flip the display both horizontally (A1) and vertically (C8)"
	oled_command(0x40); 
	
	// "Set Segment Re-map", Table 9-4 p.33. A1h flips the display
	// horizontally (column 127 maps to SEG0, instead of column 0).
	oled_command(0xA1);
	
	// "Set COM Output Scan Direction", Table 9-4 p.33. C8h flips the
	// display vertically (scans COM[N-1] down to COM0).
	oled_command(0xC8);
	
	// Set COM Pins HW Config, Table 9-4 p.33. 
	// row lines are physically wired internally -- 0x12 matches the
	// reset-default config. 
	oled_command(0xDA);
	oled_command(0x12);
	

	oled_command(0x81); // "Set Contrast Control", Table 9-1 p.27. 0x7F = reset-default,
	oled_command(0x7F); // mid-range brightness (0-255 range).
	
	

	oled_command(0xD9); // "Set Pre-charge Period", Table 9-5 p.34. Internal pixel charge/
	oled_command(0x22); // discharge timing. 0x22 = reset-default value for both phases.
	
	

	oled_command(0xDB); // "Set VCOMH Deselect Level", Table 9-5 p.34. 0x34 = reset-default
	oled_command(0x34); // voltage level (~0.78 x VCC) for deselected rows.
	
	// "Entire Display ON", Table 9-1 p.27. A4h = show actual RAM content
	// (not forced fully-on regardless of memory).

	oled_command(0xA4);
	
	// "Set Normal/Inverse Display", Table 9-1 p.27. A6h = normal:
	// a 1 bit in RAM lights a pixel, a 0 bit leaves it dark.
	oled_command(0xA6);
	

	oled_command(0x20); // set memort addressing mode
	oled_command(0x02); // 0000 0010 -> A1=1, A0=0 -> Page Addressing Mode (also the reset default)
	
	// Datasheet p.26, Power ON sequence: SEG/COM outputs turn on ~100ms
	// after the Display ON command is sent.
	_delay_ms(100);
	
	// Display on
	oled_command(0xAF);
}


void oled_clear(void){
	for (uint8_t page = 0; page < 8; page++) {
		oled_goto_page(page);
		oled_goto_column(0);
		for (uint8_t col = 0; col < 128; col++) {
			oled_data(0x00);
		}
	}
}

void oled_clear_line(uint8_t line)
{
	oled_goto_page(line);
	oled_goto_column(0);
	for (uint8_t col = 0; col < 128; col++) {
		oled_data(0x00);
	}
}

void oled_print(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' ';   // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			uint8_t column_byte = pgm_read_byte(&font5[index][col]);
			oled_data(column_byte);
		}
		oled_data(0x00);   // 1-column gap between characters
		str++;
	}
}

/*
* oled_print_inverted(): same as oled_print(), but black letters on a
* white background (used for the highlight bar of the selected menu item).
*
* Each byte sent to the OLED is one column of 8 pixels in the current
* page (bit = 1 -> pixel on). "~" flips all 8 bits, so lit pixels become
* dark and dark pixels become lit -> the letter is drawn inverted.
*/
void oled_print_inverted(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' ';   // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			oled_data(~pgm_read_byte(&font5[index][col]));   // ~ = inverted column
		}
		oled_data(0xFF);   // the 1-column gap is white too
		str++;
	}
}

/*
* oled_fill_to_end(): fill the rest of the current page (row), from column
* 'from' up to column 127, with the same byte.
*   pattern = 0x00 -> black (erases whatever was there before)
*   pattern = 0xFF -> white (finishes the highlight bar)
*
* In page addressing mode the column moves forward by itself after every
* data byte, so we only have to send (128 - from) bytes.
*/
void oled_fill_to_end(uint8_t from, uint8_t pattern)
{
	for (uint8_t col = from; col < 128; col++) {
		oled_data(pattern);
	}
}

/*
* oled_print_line(): print a whole row in one go and erase the rest of it.
* Writing all 128 columns overwrites the old content, so there is no need
* to clear the screen first -> no black flash (flicker).
* Each character is 6 columns wide (5 font + 1 gap), so max 21 characters.
*/
void oled_print_line(uint8_t row, const char *str)
{
	oled_pos(row, 0);
	oled_print(str);
	oled_fill_to_end(6 * strlen(str), 0x00);
}

/*
* oled_print_underlined(): same as oled_print(), but with a line under the
* text. The 5x7 font only uses bits 0-6 of each column byte; bit 7 (the
* bottom pixel of the page) is always empty, so "| 0x80" adds an underline
* without touching the letter.
*/
void oled_print_underlined(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' ';   // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			oled_data(pgm_read_byte(&font5[index][col]) | 0x80);
		}
		oled_data(0x80);   // the gap gets the underline too
		str++;
	}
}

/*
* oled_print_title(): title centred in the row, with a line under it
* across the FULL width (0x80 = only the bottom pixel of the column lit).
*   width of the text = 6 columns per character
*   start column      = (128 - width) / 2   -> centred
*/
void oled_print_title(uint8_t row, const char *str)
{
	uint8_t width = 6 * strlen(str);
	uint8_t start = (128 - width) / 2;

	oled_pos(row, 0);
	for (uint8_t col = 0; col < start; col++) {
		oled_data(0x80);                     // line left of the text
	}
	oled_print_underlined(str);              // text + line
	oled_fill_to_end(start + width, 0x80);   // line right of the text
}

/*
* oled_print_big(): print text with the 8x8 font (font8 in fonts.h).
* Each character is 8 columns wide, so max 16 characters per row.
*/
void oled_print_big(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' ';   // font tables start at ASCII space
		for (uint8_t col = 0; col < 8; col++) {
			oled_data(pgm_read_byte(&font8[index][col]));
		}
		str++;
	}
}

/* oled_print_big_inverted(): 8x8 font, black letters on white (selected letter) */
void oled_print_big_inverted(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' ';
		for (uint8_t col = 0; col < 8; col++) {
			oled_data(~pgm_read_byte(&font8[index][col]));
		}
		str++;
	}
}

/*
* Burst writing (used by the games, which redraw a lot every frame).
* oled_data() selects and deselects the display for EVERY byte. For a whole
* row (128 bytes) it is faster to select once, send all bytes, deselect once:
*     oled_pos(page, 0);
*     oled_data_begin();
*     for (...) oled_data_byte(b);
*     oled_data_end();
*/
void oled_data_begin(void)
{
	OLED_DC_PORT |= (1 << OLED_DC_PIN);    // D/C#=1 -> data (GDDRAM)
	spi_select_slave(SPI_SLAVE_DISPLAY);
}

void oled_data_byte(uint8_t data)
{
	write_column++;
	spi_transfer_byte(data);
}

void oled_data_end(void)
{
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}

/* Big (8x8) text centred in a row: start column = (128 - 8 * length) / 2 */
void oled_print_big_centered(uint8_t row, const char *str)
{
	uint8_t width = 8 * strlen(str);
	oled_pos(row, (128 - width) / 2);
	oled_print_big(str);
}

/* Normal (5x7) text centred in a row: 6 columns per character */
void oled_print_centered(uint8_t row, const char *str)
{
	uint8_t width = 6 * strlen(str);
	oled_pos(row, (128 - width) / 2);
	oled_print(str);
}

/*
* oled_progress_bar(): draw a bar over the full row, filled 'percent' %.
*   column byte 0xFF = all 8 pixels on       -> ends of the frame + filled part
*   column byte 0x81 = only top + bottom on  -> empty part (just the frame)
* Inside the frame there are 124 columns (columns 2..125).
*/
void oled_progress_bar(uint8_t row, uint8_t percent)
{
	if (percent > 100) {
		percent = 100;
	}
	uint8_t filled = (uint16_t)124 * percent / 100;   // multiply first, then divide

	oled_pos(row, 0);
	oled_data(0x00);                       // column 0: small margin
	oled_data(0xFF);                       // column 1: left end of the frame
	for (uint8_t col = 0; col < 124; col++) {
		if (col < filled) {
			oled_data(0xFF);               // filled part
		}
		else {
			oled_data(0x81);               // empty part: only the frame
		}
	}
	oled_data(0xFF);                       // column 126: right end of the frame
	oled_data(0x00);                       // column 127: margin
}

/* ===========================================================================
*  Text without printf
*  ---------------------------------------------------------------------------
*  printf()/snprintf() pull about 1.4 KB of library code into the program.
*  The screens only need "text + a number", so these small functions do that
*  job directly and keep the program memory free for the games.
* ======================================================================== */

/* Print one character with the 5x7 font (5 columns + 1 empty column) */
static void print_char(char c)
{
	uint8_t index = c - ' ';                 // font tables start at ASCII space
	for (uint8_t col = 0; col < 5; col++) {
		oled_data(pgm_read_byte(&font5[index][col]));
	}
	oled_data(0x00);
}

/* Print text that is stored in flash, e.g. oled_print_P(PSTR("Time: ")) */
void oled_print_P(PGM_P text)
{
	char c = pgm_read_byte(text);
	while (c != '\0') {
		print_char(c);
		text++;
		c = pgm_read_byte(text);
	}
}

/*
* oled_number_to_text(): write a number as decimal text, e.g. 427 -> "427".
*   width = 0 : just the digits
*   width = 5 : right-aligned with spaces in front, e.g. "  427"
* text must have room for 6 characters (5 digits + end marker).
* Returns the number of characters written.
*/
uint8_t oled_number_to_text(uint16_t value, uint8_t width, char *text)
{
	char digits[5];
	uint8_t count = 0;

	// Take the digits off from the right: 427 -> 7, 2, 4
	do {
		digits[count] = '0' + (value % 10);
		value = value / 10;
		count++;
	} while (value > 0);

	uint8_t length = 0;
	while (count + length < width) {         // spaces in front (right-align)
		text[length] = ' ';
		length++;
	}
	while (count > 0) {                      // digits in the right order: 4, 2, 7
		count--;
		text[length] = digits[count];
		length++;
	}
	text[length] = '\0';
	return length;
}

/* Print a number with the 5x7 font (width: see oled_number_to_text) */
void oled_print_number(uint16_t value, uint8_t width)
{
	char text[6];
	oled_number_to_text(value, width, text);
	oled_print(text);
}

/* Erase from where the last text ended to the end of the row.
*  Use after printing a line, so old longer text does not stay visible. */
void oled_clear_rest(void)
{
	while (write_column < 128) {
		oled_data(0x00);
	}
}

/* Whole row from flash text: print it, then erase the rest of the row */
void oled_print_line_P(uint8_t row, PGM_P text)
{
	oled_pos(row, 0);
	oled_print_P(text);
	oled_clear_rest();
}

/* Flash text centred in a row (the rest of the row is erased first) */
void oled_print_centered_P(uint8_t row, PGM_P text)
{
	uint8_t width = 6 * strlen_P(text);
	oled_clear_line(row);
	oled_pos(row, (128 - width) / 2);
	oled_print_P(text);
}

/* Title from flash text: copied to RAM first, then the normal title function */
void oled_print_title_P(uint8_t row, PGM_P text)
{
	char buf[22];
	strncpy_P(buf, text, 21);
	buf[21] = '\0';
	oled_print_title(row, buf);
}

/* A number in the big 8x8 font, centred, e.g. the score on "Game Over" */
void oled_print_big_number_centered(uint8_t row, uint16_t value)
{
	char text[6];
	oled_number_to_text(value, 0, text);
	oled_print_big_centered(row, text);
}

/*
* oled_flash(): invert the whole screen for a moment.
* 0xA7 = inverse display, 0xA6 = normal display (datasheet Table 9-1, p.27).
* The OLED controller does this in hardware, so no pixels are redrawn.
*/
void oled_flash(void)
{
	oled_command(0xA7);
	_delay_ms(80);
	oled_command(0xA6);
	_delay_ms(80);
}

/*
* oled_intro(): intro animation shown when a game is started from the menu.
*   big game title, small subtitle, loading bar that fills up (about 1.5 s),
*   two flashes, then a clear screen for the game.
* The title is a parameter, so every game can use the same animation:
*   oled_intro("PING PONG");   oled_intro("CAR RACE");
* Big font: max 16 characters.
*/
void oled_intro(const char *title)
{
	oled_clear();
	oled_print_big_centered(1, title);
	oled_print_centered(3, "TTK4155");
	oled_print_centered(7, "Loading...");

	for (uint8_t p = 0; p <= 100; p += 2) {   // 51 steps x 30 ms = about 1.5 s
		oled_progress_bar(5, p);
		_delay_ms(30);
	}

	_delay_ms(300);
	oled_flash();
	oled_flash();
	oled_clear();
}

// Remembers where the next character will be drawn, so repeated
// printf() calls continue from where the last one left off.
static uint8_t cursor_page = 0;
static uint8_t cursor_col  = 0;

// The function printf() calls once per character, once stdout points here.
int oled_putchar(char c, FILE *stream)
{
	if (c == '\n') {
		cursor_page = (cursor_page + 1) % 8;
		cursor_col = 0;
		return 0;
	}

	oled_goto_page(cursor_page);
	oled_goto_column(cursor_col);

	uint8_t index = c - ' ';   // font table starts at ASCII space
	for (uint8_t col = 0; col < 5; col++) {
		oled_data(pgm_read_byte(&font5[index][col]));
	}
	oled_data(0x00);   // 1-pixel gap between letters

// Move the cursor forward by one character's width.
// The 5x7 font is 5 pixels wide, plus 1 pixel of gap we add after
// it in oled_putchar() -- so each character takes up 6 columns total.
	cursor_col += 6;
	// If the cursor has gone past the right edge of the screen (128
	// columns wide), wrap around to the start of the next line --
	// same as a normal terminal wrapping text.
	if (cursor_col >= 128) {
		cursor_col = 0;
		
		// Move to the next page (line) down. "% 8" wraps back to
		// page 0 once you go past the last page (7) -- so printing
		// past the bottom of the screen loops back to the top,
		// instead of writing to an invalid page number.
		cursor_page = (cursor_page + 1) % 8;
	}
	return 0;
}

// Builds a FILE stream, statically, pointing at oled_putchar().
// _FDEV_SETUP_WRITE = write-only, since we never read from the display.
FILE oled_stdio = FDEV_SETUP_STREAM(oled_putchar, NULL, _FDEV_SETUP_WRITE);
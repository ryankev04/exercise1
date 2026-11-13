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
#include <string.h> // strlen()

void oled_command(uint8_t cmd)
{
	
	// &= bitwise AND  ~=bitwise NOT
	// (1 << OLED_DC_PIN) =  a mask with a single 1 in bit position 2
	// ~(....) flips it in this case it will be 11111011
	// OLED_DC_PORT &= 11111011 = forces bit 2 to 0, keeps every other bit of PORTD exactly as it was
	// So this line just for set D/C# to be zero
	OLED_DC_PORT &= ~(1 << OLED_DC_PIN); // D/C#=0 -> command register
	spi_select_slave(SPI_SLAVE_DISPLAY);
	spi_transfer_byte(cmd);
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}
/************************************************************************/
/*
 * oled_command(): Sends one controller command over SPI with D/C low.
 */
/************************************************************************/

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
	OLED_DC_PORT |= (1 << OLED_DC_PIN); // D/C#=1 -> GDDRAM
	spi_select_slave(SPI_SLAVE_DISPLAY);
	spi_transfer_byte(data);
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}
/************************************************************************/
/*
 * oled_data(): Sends one display RAM byte with D/C high and advances the software column count.
 */
/************************************************************************/


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
	oled_command(0xB0 | (page & 0x07)); // datasheet p.32
}
/************************************************************************/
/*
 * oled_goto_page(): Selects an eight-pixel-high page using the low three bits of page.
 */
/************************************************************************/

void oled_goto_column(uint8_t col)
{
	write_column = col;
	
	// Column needs 7 bits (0-127), but each command only carries 4
	// so the value is split into two halves and sent as two commands.
	oled_command(0x00 | (col & 0x0F));        // lower nibble, p.32
	oled_command(0x10 | ((col >> 4) & 0x0F)); // upper nibble, p.32
}
/************************************************************************/
/*
 * oled_goto_column(): Sends the low and high column nibbles and updates the software column count.
 */
/************************************************************************/

void oled_home(void)
{
	oled_goto_page(0);
	oled_goto_column(0);
}
/************************************************************************/
/*
 * oled_home(): Selects page 0, column 0.
 */
/************************************************************************/

void oled_pos(uint8_t row, uint8_t column)
{
	oled_goto_page(row);
	oled_goto_column(column);
}
/************************************************************************/
/*
 * oled_pos(): Selects an OLED page and column before drawing.
 */
/************************************************************************/
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
/************************************************************************/
/*
 * oled_reset(): Pulses the active-low reset line for 10 us and waits 10 ms after releasing it.
 */
/************************************************************************/


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
	oled_command(0x3F); // 0x3F (63) -> 64 MUX, matching this panel's actual 64 physical rows.
	oled_command(0xD3); // "Set Display Offset", Table 9-4 p.33. Vertical shift of RAM vs
	oled_command(0x00); // physical rows. 0x00 = no shift.
	
	// Set Display Start Line, Table 9-4 p.33. Low 6 bits pick which RAM .
	// Row appears at the physical top of the screen. 0x40 = start line 0.
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
	

	oled_command(0x20); // Set memory addressing mode.
	oled_command(0x02); // Select page addressing mode.
	
	// Wait before sending the Display ON command.
	_delay_ms(100);
	
	// Display on
	oled_command(0xAF);
}
/************************************************************************/
/*
 * oled_init(): Configures the control pins, resets the panel, and programs timing,
 * orientation, contrast, and page addressing before enabling the display.
 */
/************************************************************************/


void oled_clear(void){
	for (uint8_t page = 0; page < 8; page++) {
		oled_goto_page(page);
		oled_goto_column(0);
		for (uint8_t col = 0; col < 128; col++) {
			oled_data(0x00);
		}
	}
}
/************************************************************************/
/*
 * oled_clear(): Writes zero to all 128 columns of each of the eight pages.
 */
/************************************************************************/

void oled_clear_line(uint8_t line)
{
	oled_goto_page(line);
	oled_goto_column(0);
	for (uint8_t col = 0; col < 128; col++) {
		oled_data(0x00);
	}
}
/************************************************************************/
/*
 * oled_clear_line(): Writes zero to all 128 columns of the selected page.
 */
/************************************************************************/

void oled_print(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' '; // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			uint8_t column_byte = pgm_read_byte(&font5[index][col]);
			oled_data(column_byte);
		}
		oled_data(0x00); // 1-column gap between characters
		str++;
	}
}
/************************************************************************/
/*
 * oled_print(): Draws a RAM string using the five-column flash font and a blank
 * column after each character. The string must use printable ASCII.
 */
/************************************************************************/

void oled_print_inverted(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' '; // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			oled_data(~pgm_read_byte(&font5[index][col])); // ~ = inverted column
		}
		oled_data(0xFF); // the 1-column gap is white too
		str++;
	}
}
/************************************************************************/
/*
 * oled_print_inverted(): Inverts all eight bits of each normal-font column and fills the character gap.
 */
/************************************************************************/

void oled_fill_to_end(uint8_t from, uint8_t pattern)
{
	for (uint8_t col = from; col < 128; col++) {
		oled_data(pattern);
	}
}
/************************************************************************/
/*
 * oled_fill_to_end(): Writes 128 - from copies of pattern at the current controller position.
 * The caller positions the cursor before invoking this function.
 */
/************************************************************************/

void oled_print_line(uint8_t row, const char *str)
{
	oled_pos(row, 0);
	oled_print(str);
	oled_fill_to_end(6 * strlen(str), 0x00);
}
/************************************************************************/
/*
 * oled_print_line(): Writes normal text at the start of a page and clears the remaining columns.
 * A full row fits up to 21 characters at six columns each.
 */
/************************************************************************/

void oled_print_underlined(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' '; // font tables start at ASCII space
		for (uint8_t col = 0; col < 5; col++) {
			oled_data(pgm_read_byte(&font5[index][col]) | 0x80);
		}
		oled_data(0x80); // the gap gets the underline too
		str++;
	}
}
/************************************************************************/
/*
 * oled_print_underlined(): Draws normal-font text with bit 7 set in each glyph column and gap.
 */
/************************************************************************/

void oled_print_title(uint8_t row, const char *str)
{
	uint8_t width = 6 * strlen(str);
	uint8_t start = (128 - width) / 2;

	oled_pos(row, 0);
	for (uint8_t col = 0; col < start; col++) {
		oled_data(0x80); // line left of the text
	}
	oled_print_underlined(str);            // text + line
	oled_fill_to_end(start + width, 0x80); // line right of the text
}
/************************************************************************/
/*
 * oled_print_title(): Centers normal-font text and draws an underline across the whole page.
 */
/************************************************************************/

void oled_print_big(const char *str)
{
	while (*str) {
		uint8_t index = *str - ' '; // font tables start at ASCII space
		for (uint8_t col = 0; col < 8; col++) {
			oled_data(pgm_read_byte(&font8[index][col]));
		}
		str++;
	}
}
/************************************************************************/
/*
 * oled_print_big(): Draws printable ASCII using eight flash columns per character.
 */
/************************************************************************/

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
/************************************************************************/
/*
 * oled_print_big_inverted(): Draws the eight-column font with every pixel bit inverted.
 */
/************************************************************************/

void oled_data_begin(void)
{
	OLED_DC_PORT |= (1 << OLED_DC_PIN); // D/C#=1 -> data (GDDRAM)
	spi_select_slave(SPI_SLAVE_DISPLAY);
}
/************************************************************************/
/*
 * oled_data_begin(): Sets D/C high and selects the display for a multi-byte data transfer.
 * Position the page and column before starting the transfer.
 */
/************************************************************************/

void oled_data_byte(uint8_t data)
{
	write_column++;
	spi_transfer_byte(data);
}
/************************************************************************/
/*
 * oled_data_byte(): Sends a byte inside an active data transfer and advances the software column count.
 */
/************************************************************************/

void oled_data_end(void)
{
	spi_deselect_slave(SPI_SLAVE_DISPLAY);
}
/************************************************************************/
/*
 * oled_data_end(): Deselects the display to end the data transfer.
 */
/************************************************************************/

void oled_print_big_centered(uint8_t row, const char *str)
{
	uint8_t width = 8 * strlen(str);
	oled_pos(row, (128 - width) / 2);
	oled_print_big(str);
}
/************************************************************************/
/*
 * oled_print_big_centered(): Centers an eight-column-per-character RAM string on the selected page.
 */
/************************************************************************/

void oled_print_centered(uint8_t row, const char *str)
{
	uint8_t width = 6 * strlen(str);
	oled_pos(row, (128 - width) / 2);
	oled_print(str);
}
/************************************************************************/
/*
 * oled_print_centered(): Centers a six-column-per-character RAM string on the selected page.
 */
/************************************************************************/

void oled_progress_bar(uint8_t row, uint8_t percent)
{
	if (percent > 100) {
		percent = 100;
	}
	uint8_t filled = (uint16_t)124 * percent / 100; // multiply first, then divide

	oled_pos(row, 0);
	oled_data(0x00); // column 0: small margin
	oled_data(0xFF); // column 1: left end of the frame
	for (uint8_t col = 0; col < 124; col++) {
		if (col < filled) {
			oled_data(0xFF); // filled part
		}
		else {
			oled_data(0x81); // empty part: only the frame
		}
	}
	oled_data(0xFF); // column 126: right end of the frame
	oled_data(0x00); // column 127: margin
}
/************************************************************************/
/*
 * oled_progress_bar(): Draws a full-page-width bar with a 124-column interior.
 * Clamps percent to 100 and fills the corresponding whole columns.
 */
/************************************************************************/

/************************************************************************/
/*
 *  Text without printf
 *  These helpers draw flash strings and decimal values directly.
 */
/************************************************************************/

static void print_char(char c)
{
	uint8_t index = c - ' '; // font tables start at ASCII space
	for (uint8_t col = 0; col < 5; col++) {
		oled_data(pgm_read_byte(&font5[index][col]));
	}
	oled_data(0x00);
}
/************************************************************************/
/*
 * print_char(): Draws one printable ASCII character from the normal font plus a blank column.
 */
/************************************************************************/

void oled_print_P(PGM_P text)
{
	char c = pgm_read_byte(text);
	while (c != '\0') {
		print_char(c);
		text++;
		c = pgm_read_byte(text);
	}
}
/************************************************************************/
/*
 * oled_print_P(): Reads a flash string one byte at a time and draws it in the normal font.
 */
/************************************************************************/

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
	while (count + length < width) { // spaces in front (right-align)
		text[length] = ' ';
		length++;
	}
	while (count > 0) { // digits in the right order: 4, 2, 7
		count--;
		text[length] = digits[count];
		length++;
	}
	text[length] = '\0';
	return length;
}
/************************************************************************/
/*
 * oled_number_to_text(): Converts a uint16_t to decimal, padding on the left to the minimum width.
 * The caller provides room for max(width, digit count) plus the terminator.
 * Returns the length excluding the terminator.
 */
/************************************************************************/

void oled_print_number(uint16_t value, uint8_t width)
{
	char text[6];
	oled_number_to_text(value, width, text);
	oled_print(text);
}
/************************************************************************/
/*
 * oled_print_number(): Formats a number in a six-byte buffer and draws it in the normal font.
 * The requested minimum width must be at most five.
 */
/************************************************************************/

void oled_clear_rest(void)
{
	while (write_column < 128) {
		oled_data(0x00);
	}
}
/************************************************************************/
/*
 * oled_clear_rest(): Writes blank columns until the software column count reaches 128.
 */
/************************************************************************/

void oled_print_line_P(uint8_t row, PGM_P text)
{
	oled_pos(row, 0);
	oled_print_P(text);
	oled_clear_rest();
}
/************************************************************************/
/*
 * oled_print_line_P(): Draws a flash string from column 0 and clears the rest of the page.
 */
/************************************************************************/

void oled_print_centered_P(uint8_t row, PGM_P text)
{
	uint8_t width = 6 * strlen_P(text);
	oled_clear_line(row);
	oled_pos(row, (128 - width) / 2);
	oled_print_P(text);
}
/************************************************************************/
/*
 * oled_print_centered_P(): Clears the page, then centers and draws a flash string in the normal font.
 */
/************************************************************************/

void oled_print_title_P(uint8_t row, PGM_P text)
{
	char buf[22];
	strncpy_P(buf, text, 21);
	buf[21] = '\0';
	oled_print_title(row, buf);
}
/************************************************************************/
/*
 * oled_print_title_P(): Copies up to 21 characters from flash and draws the standard underlined title.
 */
/************************************************************************/

void oled_print_big_number_centered(uint8_t row, uint16_t value)
{
	char text[6];
	oled_number_to_text(value, 0, text);
	oled_print_big_centered(row, text);
}
/************************************************************************/
/*
 * oled_print_big_number_centered(): Formats the value without padding and centers it in the large font.
 */
/************************************************************************/

void oled_flash(void)
{
	oled_command(0xA7);
	_delay_ms(80);
	oled_command(0xA6);
	_delay_ms(80);
}
/************************************************************************/
/*
 * oled_flash(): Selects inverse display for 80 ms, then restores normal display and waits 80 ms.
 */
/************************************************************************/

void oled_intro(const char *title)
{
	oled_clear();
	oled_print_big_centered(1, title);
	oled_print_centered(3, "TTK4155");
	oled_print_centered(7, "Loading...");

	for (uint8_t p = 0; p <= 100; p += 2) { // 51 steps x 30 ms = about 1.5 s
		oled_progress_bar(5, p);
		_delay_ms(30);
	}

	_delay_ms(300);
	oled_flash();
	oled_flash();
	oled_clear();
}
/************************************************************************/
/*
 * oled_intro(): Draws the title and loading bar, flashes twice, and clears the screen.
 */
/************************************************************************/

// Remembers where the next character will be drawn, so repeated
// printf() calls continue from where the last one left off.
static uint8_t cursor_page = 0;
static uint8_t cursor_col  = 0;

int oled_putchar(char c, FILE *stream)
{
	if (c == '\n') {
		cursor_page = (cursor_page + 1) % 8;
		cursor_col = 0;
		return 0;
	}

	oled_goto_page(cursor_page);
	oled_goto_column(cursor_col);

	uint8_t index = c - ' '; // font table starts at ASCII space
	for (uint8_t col = 0; col < 5; col++) {
		oled_data(pgm_read_byte(&font5[index][col]));
	}
	oled_data(0x00); // 1-pixel gap between letters

	// Move the cursor forward by one character's width.
	// The 5x7 font is 5 pixels wide, plus 1 pixel of gap we add after
	// it in oled_putchar() -- so each character takes up 6 columns total.
	cursor_col += 6;
	// After writing the glyph, check whether the cursor reached column 128.
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
/************************************************************************/
/*
 * oled_putchar(): Writes one printable character for the OLED stdio stream.
 * A newline moves to the next page. The software cursor also advances
 * to the next page when its column reaches 128, and wraps after page 7.
 */
/************************************************************************/

// Builds a FILE stream, statically, pointing at oled_putchar().
// _FDEV_SETUP_WRITE = write-only, since we never read from the display.
FILE oled_stdio = FDEV_SETUP_STREAM(oled_putchar, NULL, _FDEV_SETUP_WRITE);
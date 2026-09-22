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

void oled_data(uint8_t data)
{
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
	oled_command(0xAE)
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
	
	
	
	
	oled_command(0xAF);
}


void oled_clear(void)
{
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
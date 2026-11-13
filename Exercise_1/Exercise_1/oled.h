/*
 * oled.h
 *
 * Created: 22.09.2026 10:43:30
 *  Author: ngabo
 */ 
#ifndef OLED_H_
#define OLED_H_

#include <stdint.h>
#include <stdio.h>
#include <avr/pgmspace.h> // PGM_P, PSTR(): text stored in flash


#define OLED_DC_DDR   DDRD
#define OLED_DC_PORT  PORTD
#define OLED_DC_PIN   PD2

#define OLED_RES_DDR  DDRD
#define OLED_RES_PORT PORTD
#define OLED_RES_PIN  PD3

void oled_command(uint8_t cmd);
void oled_data(uint8_t data);
void oled_goto_column(uint8_t col);
void oled_goto_page(uint8_t page);
void oled_home(void);
void oled_pos(uint8_t row, uint8_t column);
void oled_reset(void);
void oled_init(void);
void oled_clear(void);
void oled_clear_line(uint8_t line);
void oled_print(const char *str);
void oled_print_inverted(const char *str);            // black text on white
void oled_fill_to_end(uint8_t from, uint8_t pattern); // 0x00 black, 0xFF white
void oled_print_line(uint8_t row, const char *str);   // print + erase rest of row
void oled_print_underlined(const char *str);          // text with a line under it
void oled_print_title(uint8_t row, const char *str);  // centred + underlined, full width
void oled_print_big(const char *str);                 // 8x8 font, max 16 chars per row
void oled_print_big_centered(uint8_t row, const char *str);
void oled_print_big_inverted(const char *str); // 8x8 font, black on white

/* Text without printf (text in flash with PSTR("...")) */
void    oled_print_P(PGM_P text); // flash text at the current position
void    oled_print_number(uint16_t value, uint8_t width); // decimal; width > 0 = right-aligned
uint8_t oled_number_to_text(uint16_t value, uint8_t width, char *text); // text needs 6 bytes
void    oled_clear_rest(void);                      // erase to the end of the current row
void    oled_print_line_P(uint8_t row, PGM_P text); // whole row, rest erased
void    oled_print_centered_P(uint8_t row, PGM_P text);
void    oled_print_title_P(uint8_t row, PGM_P text);
void    oled_print_big_number_centered(uint8_t row, uint16_t value);
void oled_data_begin(void);        // fast drawing: start a burst of data bytes
void oled_data_byte(uint8_t data); // one byte (no chip-select toggling)
void oled_data_end(void);          // end the burst
void oled_print_centered(uint8_t row, const char *str);
void oled_progress_bar(uint8_t row, uint8_t percent); // 0..100 %
void oled_flash(void);                                // invert screen for a moment
void oled_intro(const char *title);                   // game intro animation
int oled_putchar(char c, FILE *stream);
extern FILE oled_stdio;


#endif // OLED_H_
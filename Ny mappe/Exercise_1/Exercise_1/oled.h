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
int oled_putchar(char c, FILE *stream);
extern FILE oled_stdio;


#endif //OLED_H_
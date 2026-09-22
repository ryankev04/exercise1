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
void oled_reset(void);
void oled_init(void);

int oled_putchar(char c, FILE *stream);
extern FILE oled_stdio;


#endif //OLED_H_
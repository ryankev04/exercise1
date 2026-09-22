/*
 * TTK4155_Exercise1_V1.c
 *
 * Created: 02/09/2026 09:24:38
 * Author : muval, ngabo
 */ 



//========================================== INCLUDES


#include "usart.h"
#include "Sram.h"
#include "Decoder.h"
#include "ADC.h"
#include "Spi.h"
#include "oled.h"
#include "Io.h"
#include "Menu.h"
#include <util/delay.h>


const char* dir_name(joystick_dir_t d)
{
	switch(d)
	{
		case JOY_LEFT: return "LEFT";
		case JOY_RIGHT: return "RIGHT";
		case JOY_UP: return "UP";
		case JOY_DOWN: return "DOWN";
		default: return "NEUTRAL";
	}
}




int main(void)
{
	
uart1_init();
	FILE *uart_stream = fdevopen(uart_putchar, uart_getchar);   // save the pointer!
	stdout = uart_stream;

	spi_master_init();
	oled_init();

	printf("Hello 2 from ATmega162!\n");   // -> RS232

	

	//stdout = uart_stream;                    // switch back
	//printf("Back on RS232\n");

Xmem_init();	
		
//adc_clk_init();
//Latch_test();
//SRAM_test();
//SRAM_single_test();
//decoder_test();
joystick_calibrate();
//joy_read();

spi_master_init();
oled_init();

//stdout = &oled_stdio;
//printf("HELLO OLED");                    // -> display
oled_clear();

static menu_t main_menu = {
	.title = "Main Menu",
	.items = { "Start Game", "Options", "About" },
	.item_count = 3,
	.submenus = { NULL, NULL, NULL }
};

int8_t choice = menu_run(&main_menu);
printf("Selected: %d\n", choice);

	while (1)
	{
		//joy_slider_read();
		//joystick_dir_t dir = joy_dir();
		//printf("Dir: %s\n", dir_name(dir));
		//_delay_ms(200);
	}
}


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


int main(void){
	
uart1_init();
fdevopen(uart_putchar, uart_getchar);
printf("Hello 2 from ATmega162!\n");

Xmem_init();	
		

//Latch_test();
//SRAM_test();
//SRAM_single_test();
decoder_test();
	
	while (1){
		
		if (uart1_available()){
			uint8_t c = uart1_receive();
			printf("%c" , c);
		}
	}
}
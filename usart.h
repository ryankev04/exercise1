/*
 * usart.h
 *
 * Created: 09.09.2026 12:35:59
 *  Author: ngabo
 */ 



#ifndef USART_H_
#define USART_H_
//========================================== INCLUDES

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h> 


//========================================== DEFINES
/*
* F_CPU: CPU / oscillator frequency used in this calculation. Our board uses a 4.9152 MHz crystal.
* UBRR_VAL: The resulting value from the UBRR formula by a baud rate and CPU frequency.
* RX_BUFFER_SIZE:
	* Defines the size of the software receive buffer.
	* Here the array contains 32 byte-sized locations.
*/


#define F_CPU  4915200UL
#define BAUD     9600UL
#define SRAM_START 0x1400
#define SRAM_SIZE  0xC00      // 3072 bytes = 3 KiB
#define UBRR_VAL ((F_CPU/(16UL*BAUD)) - 1)
#define RX_BUFFER_SIZE 32


//========================================== Functions

void uart1_init(void);
void uart1_send(uint8_t data);
uint8_t uart1_available(void);
char    uart1_receive(void);

// stdio adapters (used with fdevopen, or FDEV_SETUP_STREAM)

int uart_putchar(char c, FILE *stream);
int uart_getchar(FILE *stream);

//========== variables

extern volatile uint8_t rx_data[RX_BUFFER_SIZE];
extern volatile uint8_t rx_head;
extern volatile uint8_t rx_tail;

#endif //USART_H_

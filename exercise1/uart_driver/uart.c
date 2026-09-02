
#include "uart.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

volatile char received_char;
volatile uint8_t received_ready = 0;

ISR(USART1_RXC_vect)
{
received_char = UDR1;
received_ready = 1;
}


void uart1_init(void){
UBRR1H = 0;
UBRR1L = 31;
/*
* UBRR = \frac{f_{osc}}{16*Baud} - 1.
* 4.9152 MHz crystal is being used, therefore, with 9600 baud: UBRR=31.
* H and L are two pieces of a large number.
* UBRR is a 12-bit register. UBRRH takes the 4 MSB, while UBRRL takes the remaining 8 LSB.
*/


UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
/*
* Enable:
* USART1 Receive.
* USART1 Transmit.
* Receive-Complete interrupt.
*/


UCSR1C = (1 << URSEL1) | (1 << UCSZ11) | (1 << UCSZ10);


sei();
}


void uart1_send(char c)
{
while (!(UCSR1A & (1 << UDRE1)))
{
// Wait until transmit buffer is free
}

UDR1 = c;
}


uint8_t uart1_available(void)
{
return received_ready;
}

char uart1_receive(void)
{
received_ready = 0;
return received_char;
}


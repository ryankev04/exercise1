/*
 * usart.c
 *
 * Created: 09.09.2026 12:35:33
 *  Author: ngabo
 */ 



/************************************************************************/
/*
 * ============================  includes
 */
/************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h>

/************************************************************************/
/*
 * ============================ Private includes
 */
/************************************************************************/
#include "usart.h"



/************************************************************************/
/*
 * ========================================== GLOBALLY DECLARED VARIABLES
 */
/************************************************************************/

/*
* rx_data[]: Array containing bytes that have not yet been processed by main() yet.
* rx_head: Index identifying the position where the next received byte will be written.
* rx_tail: Index identifying the oldest unread byte currently waiting for the main program.

* volatile tells the compiler that these values may change outside the immediately visible 
	flow of the current function.
*/

volatile uint8_t rx_data[RX_BUFFER_SIZE] ; // Unread bytes received by USART1.
volatile uint8_t rx_head = 0;  // Next slot written by the receive ISR.
volatile uint8_t rx_tail = 0;   // Oldest unread slot in the receive ring.




ISR(USART1_RXC_vect)
{
	uint8_t received_byte = UDR1;
	uint8_t next_position = rx_head + 1;
	if (next_position == RX_BUFFER_SIZE) {
		next_position = 0;
	}
	if (next_position != rx_tail) {
		rx_data[rx_head] = received_byte;
		rx_head = next_position;
	}
}
/************************************************************************/
/*
 * ISR(USART1_RXC_vect): Queues the byte read from UDR1. One ring slot
 * stays unused to distinguish full from empty. A new byte is discarded if
 * advancing the head would reach the tail.
 */
/************************************************************************/

void uart1_init(void)
{
	UBRR1H = (uint8_t)(UBRR_VAL >> 8);
	UBRR1L = (uint8_t)UBRR_VAL;
	
	UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
	
	UCSR1C = (1 << URSEL1) | (1 << UCSZ11) | (1 << UCSZ10);
	
	sei();
}
/************************************************************************/
/*
 * uart1_init(): Configures asynchronous USART1 for 8 data bits, no
 * parity, and one stop bit. UBRR_VAL is 31 for 9600 baud at 4.9152 MHz in
 * normal-speed mode. Enables RX, TX, the receive interrupt, and global
 * interrupts.
 */
/************************************************************************/

void uart1_send(uint8_t data)
{
	while (!(UCSR1A & (1 << UDRE1))){/*wait*/}

	UDR1 = data;
}
/************************************************************************/
/*
 * uart1_send(): Waits for the transmit data register to accept a byte,
 * then writes it.
 */
/************************************************************************/

uint8_t uart1_available(void)
{
	return (rx_head != rx_tail);
}
/************************************************************************/
/*
 * uart1_available(): Returns 1 if there is an unread byte in the ring,
 * otherwise 0.
 */
/************************************************************************/

char uart1_receive(void)
{
	uint8_t byte_to_return = rx_data[rx_tail];
	rx_tail = rx_tail + 1;
	if (rx_tail == RX_BUFFER_SIZE){
		rx_tail = 0;
	}
	return byte_to_return;
}
/************************************************************************/
/*
 * uart1_receive(): Returns the oldest buffered byte and advances the tail
 * with wraparound. The caller checks uart1_available() first.
 */
/************************************************************************/

int uart_putchar(char c, FILE *stream) {
	if (c == '\n') {
		uart1_send('\r');
	}
	uart1_send((uint8_t)c);
	return 0;
}
/************************************************************************/
/*
 * uart_putchar(): Sends one stdio character, inserting a carriage return
 * before a newline.
 */
/************************************************************************/

int uart_getchar(FILE *stream) {
	while (!uart1_available()) {}
	return uart1_receive();
}
/************************************************************************/
/*
 * uart_getchar(): Waits for an unread byte and consumes it for the stdio
 * stream.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * Learning Curve Attack 1.0V
 *
 * =============Abbreviations
 *
 *
 * RETI:
 * Return from Interrupt
 *
 * RXEN1 / TXEN1:
 * RX: Receive.
 * TX: Transmit.
 * EN: Enable.
 * 1: USART1.
 * Description:
 * These are individual bits inside of UCSR1B. Check out page 306, 305, 304 of the datasheet.
 * 			You'll notice the register names and their contents, as well as what each bit represents.
 * Setting RXEN1 or TXEN1 to 1 will turn on USART1's receiver or transmitter hardware, respectively.
 *
 * RXC1:
 * RX: Receive.
 * C: Complete.
 * 1: USART1.
 * Description:
 * Indicates that USART1 has received unread data.
 * =1 means USART1 has received a character that has not yet been read from UDR1.
 * Reading UDR1 retrieves the received data, and also causes the Receive Complete condition to
 * 			no longer remain asserted for that received byte.
 *
 * RXCIE1:
 * RX: Receive.
 * C: Complete.
 * IE: Interrupt Enable.
 * 1: USART1.
 * Description:
 * Setting this bit to "1", will allow USART1 to cause an interrupt on received character.
 *
 * UBRR1H / UBRR1L:
 * UBRR: USART Baud Rate Register
 * 1: USART Number 1.
 * H: High byte.
 * L: Low byte.
 * Description:
 * UBRR1 is 12 bits wide. UBRR1H contains 4 MSB, UBRR1L contains the remaining 8 LSB.
 *
 * UCSR1A / UCSR1B / UCSR1C:
 * UCSR: USART Control and Status Register
 * 1: USART 1.
 * A: Register A.
 * Page 305 of datasheet. The contents of Register name UCSR1A is 8-bit large, from MSB to LSB:
 * 			RXC1, TXC1, UDRE1, FE1, DOR1, UPE1, U2X1, MPCM1.
 * B: Register B.
 * Page 306 of datasheet. The contents of Register name UCSR1B is 8-bit large, from MSB to LSB:
 * 			RXCIE1, TXCIE1, UDRIE1, RXEN1, TXEN1, UCSZ12, RXB81, TXB81.
 * C: Register C.
 * Page 305 of datasheet. The contents of Register name UCSR1C is 8-bit large, from MSB to LSB:
 * 			URSEL1, UMSEL1, UPM11, UPM10, USBS1, UCSZ11, UCSZ10, UCPOL1
 *
 * UCSZ10:
 * UCSZ: USART Character Size.
 * 1: USART1.
 * 0: Character-size bit 0.
 * UCSZ11:
 * 1: Character-size bit 1.
 * UCSZ12:
 * 2: Character-size bit 2.
 *
 * UDRE1:
 * UDR: USART Data Register.
 * E: Empty.
 * 1: USART1.
 * Description:
 * UDRE1 is a status bit for UCSR1A.
 * Indicates whether the USART1 transfer buffer is ready to accept a new byte. UDRE1=1, transfer
 * 			buffer is empty. UDRE1=0, transfer buffer is not empty.
 * =1 does not mean the previous byte has finished, it may still be inside the transmit shift
 * 			register. TXC1 should be used to know whether the entire transmission
 * 			has been completed.
 *
 * URSEL1:
 * USART Register Select.
 * 1: USART1.
 * Description:
 * UCSR1C and UBRR1H share an I/O address on the ATmega162.
 * URSEL1= 1 identifies the write as being intended for UCSR1C. Otherwise for UBRR1H.
 *
 * USART1_RXC_vect:
 * USART1: USART number 1.
 * RXC: Receive Complete.
 * vect: Vector.
 * Description:
 * When interrupt X happens, execution goes here: Y. Y itself is an entry in the interrupt vector
 * 			table, defined here.
 *
 *
 * ===============Libraries
 *
 * <avr/io.h>:
 * AVR device-specific I/O definitions.
 * Gives symbolic names such as UCSR1A, UDR1, RXEN1, UDRE1, etc.
 *
 *
 * <avr/interrupt.h>:
 * AVR Libc software header that gives conventional C interfaces for AVR interrupts, providing macros
 * 		functions needed for AVR interrupts. ISR(), cli(), sei().
 *
 *
 * iom162.h, research if you want to inspect the abbreviations yourself:
 * https://github.com/avrdudes/avr-libc/blob/main/include/avr/iom162.h
 *
 *
 * ===============Functions
 *
 * sei()
 * Enable all interrupts globally.
 * At the actual AVR CPU level there is an `I` bit in the status register `SREG`.
 * SREG: Status Register. An 8-bit register belonging to the CPU, not to UART. Composed of (from MSB
 * 			to LSB) I, T, H, S, V, N, Z, C.
 * I = Global Interrupt Enable.
 * If I=0, interrupts are disabled globally regardless of individual settings. However, sei()
 * 			reconfigures this bit.
 *
 * cli()
 * Disable all interrupts globally.
 *
 * ISR(vector)
 * Interrupt Service Routine.
 * Hardware calls it whenever interrupt happens.
 *
 * fdevopen()
 * AVR Libc function used to connect standard C input/output to user-defined functions.
 * fdevopen() itself does not transmit or receive USART data.
 * fdevopen(uart_putchar, uart_getchar) tells functions such as printf() to use uart_putchar()
 * 		for output, and functions such as getchar() to use uart_getchar() for input.
 */
/************************************************************************/

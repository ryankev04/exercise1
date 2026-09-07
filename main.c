/*
 * TTK4155_Exercise1_V1.c
 *
 * Created: 02/09/2026 09:24:38
 * Author : muval, ngabo
 */ 
/*
* Learning Curve Attack 1.0V

=============Abbreviations


* RETI:
	* Return from Interrupt

* RXEN1 / TXEN1:
	* RX: Receive.
	* TX: Transmit.
	* EN: Enable.
	* 1: USART1.
	* Description:
		* These are individual bits inside of UCSR1B. Check out page 306, 305, 304 of the datasheet. 
			You'll notice the register names and their contents, as well as what each bit represents.
		* Setting RXEN1 or TXEN1 to 1 will turn on USART1's receiver or transmitter hardware, respectively.

* RXC1:
	* RX: Receive.
	* C: Complete.
	* 1: USART1.
	* Description:
		* Indicates that USART1 has received unread data.
		* =1 means USART1 has received a character that has not yet been read from UDR1.
		* Reading UDR1 retrieves the received data, and also causes the Receive Complete condition to
			no longer remain asserted for that received byte.

* RXCIE1:
	* RX: Receive.
	* C: Complete.
	* IE: Interrupt Enable.
	* 1: USART1.
	* Description:
		* Setting this bit to "1", will allow USART1 to cause an interrupt on received character.

* UBRR1H / UBRR1L:
	* UBRR: USART Baud Rate Register
	* 1: USART Number 1.
	* H: High byte.
	* L: Low byte.
	* Description:
		* UBRR1 is 12 bits wide. UBRR1H contains 4 MSB, UBRR1L contains the remaining 8 LSB.
	
* UCSR1A / UCSR1B / UCSR1C: 
	* UCSR: USART Control and Status Register
	* 1: USART 1.
	* A: Register A.
		* Page 305 of datasheet. The contents of Register name UCSR1A is 8-bit large, from MSB to LSB:
			RXC1, TXC1, UDRE1, FE1, DOR1, UPE1, U2X1, MPCM1.
	* B: Register B.
		* Page 306 of datasheet. The contents of Register name UCSR1B is 8-bit large, from MSB to LSB:
			RXCIE1, TXCIE1, UDRIE1, RXEN1, TXEN1, UCSZ12, RXB81, TXB81.
	* C: Register C.
		* Page 305 of datasheet. The contents of Register name UCSR1C is 8-bit large, from MSB to LSB:
			URSEL1, UMSEL1, UPM11, UPM10, USBS1, UCSZ11, UCSZ10, UCPOL1
	
* UCSZ10:
	* UCSZ: USART Character Size.
	* 1: USART1.
	* 0: Character-size bit 0.
* UCSZ11:
	* 1: Character-size bit 1.
* UCSZ12:
	* 2: Character-size bit 2.

* UDRE1:
	* UDR: USART Data Register.
	* E: Empty.
	* 1: USART1.
	* Description: 
		* UDRE1 is a status bit for UCSR1A.
		* Indicates whether the USART1 transfer buffer is ready to accept a new byte. UDRE1=1, transfer
			buffer is empty. UDRE1=0, transfer buffer is not empty. 
		* =1 does not mean the previous byte has finished, it may still be inside the transmit shift
			register. TXC1 should be used to know whether the entire transmission
			has been completed.

* URSEL1:
	* USART Register Select.
	* 1: USART1.
	* Description:
		* UCSR1C and UBRR1H share an I/O address on the ATmega162.
		* URSEL1= 1 identifies the write as being intended for UCSR1C. Otherwise for UBRR1H.

* USART1_RXC_vect:
	* USART1: USART number 1.
	* RXC: Receive Complete.
	* vect: Vector.
	* Description:
		* When interrupt X happens, execution goes here: Y. Y itself is an entry in the interrupt vector
			table, defined here. 


===============Libraries

* <avr/io.h>:
	* AVR device-specific I/O definitions.
	* Gives symbolic names such as UCSR1A, UDR1, RXEN1, UDRE1, etc.


* <avr/interrupt.h>:
	* AVR Libc software header that gives conventional C interfaces for AVR interrupts, providing macros
		functions needed for AVR interrupts. ISR(), cli(), sei().
	* 

* iom162.h, research if you want to inspect the abbreviations yourself:
	* https://github.com/avrdudes/avr-libc/blob/main/include/avr/iom162.h


===============Functions

* sei()
	* Enable all interrupts globally.
	* At the actual AVR CPU level there is an `I` bit in the status register `SREG`.
		* SREG: Status Register. An 8-bit register belonging to the CPU, not to UART. Composed of (from MSB
			to LSB) I, T, H, S, V, N, Z, C.
		* I = Global Interrupt Enable.
		* If I=0, interrupts are disabled globally regardless of individual settings. However, sei()
			reconfigures this bit.

* cli()
	* Disable all interrupts globally.

* ISR(vector)
	* Interrupt Service Routine.
	* Hardware calls it whenever interrupt happens. 

* fdevopen()
	* AVR Libc function used to connect standard C input/output to user-defined functions. 
	* fdevopen() itself does not transmit or receive USART data.
	* fdevopen(uart_putchar, uart_getchar) tells functions such as printf() to use uart_putchar()
		for output, and functions such as getchar() to use uart_getchar() for input.
*/


//========================================== INCLUDES

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h>

//========================================== DEFINES

#define F_CPU  4915200UL
#define BAUD     9600UL
#define UBRR_VAL ((F_CPU/(16UL*BAUD)) - 1)
#define RX_BUFFER_SIZE 32 

/*
* F_CPU: CPU / oscillator frequency used in this calculation. Our board uses a 4.9152 MHz crystal.
* UBRR_VAL: The resulting value from the UBRR formula by a baud rate and CPU frequency.
* RX_BUFFER_SIZE:
	* Defines the size of the software receive buffer.
	* Here the array contains 32 byte-sized locations.
*/

//========================================== GLOBALLY DECLARED VARIABLES

volatile uint8_t rx_data[RX_BUFFER_SIZE] ;   // Array that will hold out bytes send
volatile uint8_t rx_head = 0;  
volatile uint8_t rx_tail = 0;   


/*
* rx_data[]: Array containing bytes that have not yet been processed by main() yet.
* rx_head: Index identifying the position where the next received byte will be written.
* rx_tail: Index identifying the oldest unread byte currently waiting for the main program.

* volatile tells the compiler that these values may change outside the immediately visible 
	flow of the current function.
*/



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
/*
* USART1 Receive Complete Interrupt Service Routine.
* This function is an interrupt handler.
* This function is automatically executed when USART1 receives a complete character, RXCIE1 is enabled, and
*	global interrupts are enabled through the I-bit in SREG.

* USART1_RXC_vect comes from device-specific AVR header, i.e. ATmega162 definitions, see page 59 of
	ATmega162/V datasheet. Internally, it maps to something uglier, but AVR-Libc thankfully gives that
	vector entry the C name:

* ISR's job here: Read the byte immediately, determine the location of the next buffer position, 
	store the byte if there is room, then advance rx_head.
* (next_position == RX_BUFFER_SIZE) controls wrap-around.
* (next_position != rx_tail) checks for full buffer. If this is false, the buffer is full. 
*/ 


void uart1_init(void){
	UBRR1H = (uint8_t)(UBRR_VAL >> 8);
	UBRR1L = (uint8_t)UBRR_VAL;
	
	UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
	
	UCSR1C = (1 << URSEL1) | (1 << UCSZ11) | (1 << UCSZ10);
	
	sei();
}
/*
* Initialise USART1.
* Configuration: 
	* Baud rate:	9600 baud
	* Data bits:	8.
	* Parity:		none.
	* Mode:			Asynchronous
	* Receiver:		Enabled
	* Transmitter:	Enabled
	* RX interrupt: Enabled

* UBRR = \frac{f_{osc}}{16*Baud} - 1.
* 4.9152 MHz crystal is being used, therefore, with 9600 baud: UBRR=31.
* H and L are two pieces of a large number.
	* UBRR is a 12-bit register. UBRRH takes the 4 MSB, while UBRRL takes the remaining 8 LSB.  
	
* UCSR1B = USART1 Control and Status Register B
	* Enables receiver hardware, transmitter hardware, and interrupt for receiving a character successfully
		for USART1, respectively in context of code line.
	
* UCSR1C = USART1 Control and Status Register C.
	* On the ATmega162, UCSR1C and UBRR1H share an address. URSEL1 (USART Register Select) = 1 tells the
		hardware that this write is intended for UCSR1C rather than UBRR1H.
	* UCSZ11 and UCSZ10 both configures USART register siZe. If you format UCSZ12 UCSZ11 UCSZ10, range 000
		to 111 changes the character size from 5 to 9 respectively-- because 100, 101, 110 are reserved.
		Here we've configured data bits to 8. 
		
* sei() enables global interrupts. See Learning Curve Attack: Functions.
		
* Defaults:
* UPM11:UPM10 remain 00: No parity.
* USBS1 remains 0: One stop bit.
* UMSEL1 remains 0: Asynchronous USART mode.
*/


void uart1_send(uint8_t data)
{
	while (!(UCSR1A & (1 << UDRE1))){/*wait*/}

	UDR1 = data;
}
/*
* Send one character/byte through USART1. 

* UCSR1A = USART1 Control and Status Register A.
	* UDRE1 = 1 means the USART1 transmit data register is ready to accept another character/byte.
	* The while condition, masks every bit except UDRE1. "!" makes the while loop continue only if UDRE1
		=0. 
	* So a wait is conditioned if the transmit data register is not ready.
	* The term for this is called "polling". 
	
* UDR1 = data, places the byte into USART1's transmit data register. The hardware serializes the byte and
	sends it through the TXD1 pin. 
*/




uint8_t uart1_available(void)
{
	return (rx_head != rx_tail);
}
/*
* Check whether at least one unread received byte exists. If both are equal, that is the condition in which
	the buffer is empty. Otherwise, contents exist. Returns 1 if character is available, meaning ready to
	be parsed, 0 otherwise.
*/

char uart1_receive(void)
{
	uint8_t byte_to_return = rx_data[rx_tail];
	rx_tail = rx_tail + 1;
	if (rx_tail == RX_BUFFER_SIZE){
		rx_tail = 0;
	}
	return byte_to_return;
}
/*
* Identify the oldest byte. 
* NOTE! uart1_receive() assumes that data is actually available. The intended usage is therefore:
* if (uart1_available()){
*	 uint8_t c = uart1_receive();
* }
* Calling uart1_receive() on an empty buffer would return data from whatever array position tail 
	currently references even though that byte is not logically waiting to be read. I.e. trash. Retrieve
	the most recently received character. This is so that uart1_available reports this character as read. 

* This stores only one character. A robust USART driver would use a circular buffer to prevent overwritten
	characters. 
*/

int uart_putchar(char c, FILE *stream) {
	if (c == '\n') {
		uart1_send('\r');
	}
	uart1_send((uint8_t)c);
	return 0;
}
/*
* Adapter between stdio and our USART transmission function.
* printf() outputs one character at a time, fdevopen() expects this purpose.
* uart_putchar() takes each character produced by printf() and sends it through uart1_send().
* Whenever printf requests '\n', this function sends '\r' first, thereafter it sends the requested.
* So printf("Hello!\n") results in H e l l o CR LF.
*/

int uart_getchar(FILE *stream) {
	while (!uart1_available()) {}
	return uart1_receive();
}
/*
* Adapter between stdio and our USART receive buffer. 
*/


int main(void){
	
	uart1_init();
	fdevopen(uart_putchar, uart_getchar);
	printf("Hello from ATmega162!\n");
	
	while (1){
		if (uart1_available()){
			uint8_t c = uart1_receive();
			printf("%c");
		}
	}
}
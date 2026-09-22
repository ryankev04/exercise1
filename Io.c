/*
 * Io.c
 *
 * Created: 22.09.2026 14:49:54
 *  Author: ngabo
 */ 
#include "Io.h"
#include "Spi.h"
#include "usart.h"
#include <util/delay.h>

io_joystick_t io_read_joystick(void)
{
	io_joystick_t result;

	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x03);   // command: "Joystick" (IO board doc)
	_delay_us(40);              // command -> first data byte gap (board doc)

// Sending 0x00 here is a dummy byte -- we don't care what we send,
// only what comes back. SPI can't receive without also transmitting.
	result.x = spi_transfer_byte(0x00);
	_delay_us(2);                // data -> data gap (board doc)

	result.y = spi_transfer_byte(0x00);
	_delay_us(2);

	result.btn = spi_transfer_byte(0x00);

	spi_deselect_slave(SPI_SLAVE_IO);

	return result;
}
/*
* io_read_buttons(): reads the tactile switches + 5-way nav stick,
* command 0x04, per the IO board doc's own table:
*   right: [0 0 R6 R5 R4 R3 R2 R1]
*   left:  [0 L7 L6 L5 L4 L3 L2 L1]
*   nav:   [0 0 0 Up Down Left Right Btn]
*
* NOTE: this is a DIFFERENT physical control than io_read_joystick()'s
* analog stick (command 0x03) -- the board doc separately lists
* "Tactile switches and 5-way navigation stick" as distinct hardware.
*/
io_buttons_t io_read_buttons(void)
{
	io_buttons_t result;

	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x04);   // command: "Buttons"
	_delay_us(40);

	result.right = spi_transfer_byte(0x00);
	_delay_us(2);

	result.left = spi_transfer_byte(0x00);
	_delay_us(2);

	result.nav = spi_transfer_byte(0x00);

	spi_deselect_slave(SPI_SLAVE_IO);

	return result;
}

/*
* io_led_set(): turns one of the IO board's own LEDs (LED_N, 0-5) on or off.
* Command 0x05, per the board doc: "LED_N on/off -- on/off with a value
* other than 0 will turn on the LED".
*
* This is a WRITE command -- only sends, never reads a reply, so no
* read-timing (2us between data bytes) applies per the doc's own wording
* ("for read-commands"). The 40us command->data gap still applies, since
* nothing in the doc scopes that one to reads only.
*/
void io_led_set(uint8_t led_n, uint8_t on)
{
	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x05);
	_delay_us(40);

	spi_transfer_byte(led_n);
	spi_transfer_byte(on);

	spi_deselect_slave(SPI_SLAVE_IO);
}
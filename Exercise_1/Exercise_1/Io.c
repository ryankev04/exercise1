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

io_joystick_t io_read_joystick(void){
	io_joystick_t result;

	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x03); // command: "Joystick" (IO board doc)
	_delay_us(40);           // command -> first data byte gap (board doc)

	// Send dummy bytes to provide the SPI clocks for the reply.
	result.x = spi_transfer_byte(0x00);
	_delay_us(2); // data -> data gap (board doc)

	result.y = spi_transfer_byte(0x00);
	_delay_us(2);

	result.btn = spi_transfer_byte(0x00);

	spi_deselect_slave(SPI_SLAVE_IO);

	return result;
}
/************************************************************************/
/*
 * io_read_joystick(): Requests the analog joystick sample with
 * command 0x03. With chip select held active, clocks out X, Y,
 * and the joystick button byte in that order. The code waits
 * 40 us after the command and 2 us between reply bytes.
 */
/************************************************************************/

io_buttons_t io_read_buttons(void)
{
	io_buttons_t result;

	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x04); // command: "Buttons"
	_delay_us(40);

	result.right = spi_transfer_byte(0x00);
	_delay_us(2);

	result.left = spi_transfer_byte(0x00);
	_delay_us(2);

	result.nav = spi_transfer_byte(0x00);

	spi_deselect_slave(SPI_SLAVE_IO);

	return result;
}
/************************************************************************/
/*
 * io_read_buttons(): Reads the tactile switches and five-way
 * navigation stick with command 0x04. The reply arrives as three
 * raw bytes, most-significant bit first in the layouts below.
 *
 *   right: [0 0 R6 R5 R4 R3 R2 R1]
 *   left:  [0 L7 L6 L5 L4 L3 L2 L1]
 *   nav:   [0 0 0 Up Down Left Right Btn]
 */
/************************************************************************/

void io_led_set(uint8_t led_n, uint8_t on)
{
	if (led_n > 5) {
		return;
	}
	
	spi_select_slave(SPI_SLAVE_IO);

	spi_transfer_byte(0x05); // command: "LED on/off"
	_delay_us(40);           // command -> first data byte gap (board doc)

	spi_transfer_byte(led_n); // which LED, 0-5
	spi_transfer_byte(on);    // 0 = off, anything else = on

	spi_deselect_slave(SPI_SLAVE_IO);
}
/************************************************************************/
/*
 * io_led_set(): Sets one board LED through command 0x05.
 * Accepts indices 0 through 5 and ignores higher values. After
 * the 40 us command delay, sends the index and on/off value.
 * Zero turns the LED off and any nonzero value turns it on.
 */
/************************************************************************/

void io_led_groups_update(uint8_t left_active, uint8_t right_active){
	static uint8_t previous_state = 0xFF; // Cached left and right states in bits 0 and 1.
	uint8_t state = (left_active ? 0x01 : 0x00)
	| (right_active ? 0x02 : 0x00);

	if ((state & 0x01) != (previous_state & 0x01)) {
		for (uint8_t led = 0; led < 3; led++) {
			io_led_set(led, left_active);
		}
	}

	if ((state & 0x02) != (previous_state & 0x02)) {
		for (uint8_t led = 3; led < 6; led++) {
			io_led_set(led, right_active);
		}
	}

	previous_state = state;
}
/************************************************************************/
/*
 * io_led_groups_update(): Maps left to LED indices 0-2 and
 * right to indices 3-5. Each bank is compared with its cached
 * on/off state, so only banks whose requested state changed
 * are written. On the first call, the comparison assumes both
 * banks were on because the cache starts at 0xFF.
 */
/************************************************************************/


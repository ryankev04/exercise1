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
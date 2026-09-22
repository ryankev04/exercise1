/*
 * Spi.h
 *
 * Created: 22.09.2026 10:43:06
 *  Author: ngabo
 */ 

#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>
#include <avr/io.h>

#define SPI_DDR   DDRB
#define SPI_PORT  PORTB
#define SPI_MOSI  PB5
#define SPI_MISO  PB6
#define SPI_SCK   PB7

#define SS_DISPLAY_DDR   DDRB
#define SS_DISPLAY_PORT  PORTB
#define SS_DISPLAY_PIN   PB4     // using the chip's "SS" pin here

#define SS_IO_DDR    DDRB
#define SS_IO_PORT   PORTB
#define SS_IO_PIN    PB0         // SS2

typedef enum {
	SPI_SLAVE_DISPLAY,   // DISP_CS -- the OLED's SSD1309 controller
	SPI_SLAVE_IO         // IO_CS -- the User-IO board's
	
} spi_slave_t;

void spi_master_init(void);
void spi_select_slave(spi_slave_t slave);
void spi_deselect_slave(spi_slave_t slave);
uint8_t spi_transfer_byte (uint8_t data);
void spi_write_bytes(const uint8_t *data, uint16_t len);
void spi_read_bytes(uint8_t *buffer, uint16_t len);
#endif //SPI_H_
/*
 * sd_card.c
 *
 * microSD card in SPI mode, with software SPI on four I/O pins.
 *
 * Reference: "SD Specifications Part 1, Physical Layer Simplified
 * Specification" (free from sdcard.org), chapter 7 "SPI Mode":
 *   7.2.1  start-up in SPI mode (CMD0, CMD8, ACMD41, CMD58)
 *   7.2.3  reading a block (CMD17, data token 0xFE)
 *   7.3.1  command format (6 bytes)
 *   7.3.2  responses R1, R3, R7
 *
 * SPI mode 0 is used: the clock idles low, both sides read the data line
 * on the RISING edge and change it after the FALLING edge.
 */

#include "sam.h"
#include "sd_card.h"
#include "timer.h"

/* ---------------------------------------------------------------------------
 *  Pins (double pin row at the end of the Due; our Node 2 code does not use them)
 *
 *    SD board   Due pin   SAM3X pin
 *    CLK   <-   D46       PC17
 *    SI    <-   D48       PC15
 *    SO    ->   D50       PC13
 *    CS    <-   D52       PB21      <- on port B, the other three on port C
 *
 *  CLK, SI and SO must be on the same port (SD_PIO). CS has its own port
 *  (SD_CS_PIO). To use other pins, only change these lines.
 * ------------------------------------------------------------------------- */
#define SD_PIO       PIOC
#define SD_PIO_ID    ID_PIOC
#define SD_SCK       PIO_PC17    // D46 -> CLK  (clock, we drive it)
#define SD_MOSI      PIO_PC15    // D48 -> SI   (data from the Due to the card)
#define SD_MISO      PIO_PC13    // D50 <- SO   (data from the card to the Due)

#define SD_CS_PIO    PIOB
#define SD_CS_PIO_ID ID_PIOB
#define SD_CS        PIO_PB21    // D52 -> CS   (chip select, 0 = card listens)

/* SD commands used (spec section 7.3.1.3) */
#define CMD0_GO_IDLE        0    // reset the card, enter SPI mode
#define CMD8_SEND_IF_COND   8    // "can you run at 3.3 V?" (only SD version 2 cards know it)
#define CMD16_SET_BLOCKLEN  16   // block size = 512 bytes (only needed for old cards)
#define CMD17_READ_BLOCK    17   // read one sector
#define CMD55_APP_CMD       55   // "the next command is an application command (ACMD)"
#define ACMD41_SEND_OP_COND 41   // start the card's initialization
#define CMD58_READ_OCR      58   // read the card type (SDHC or not)

#define R1_IDLE             0x01 // R1 answer: card is in idle state (still starting)
#define R1_ILLEGAL_COMMAND  0x04 // R1 answer: card does not know this command
#define DATA_TOKEN          0xFE // sent by the card just before the 512 data bytes

static uint8_t slow_clock = 1;        // 1 during start-up: the clock must be below 400 kHz
static uint8_t block_addressing = 0;  // 1 = SDHC/SDXC (address = sector number)
                                      // 0 = old SDSC card (address = byte number)

/* ---------------------------------------------------------------------------
 *  Software SPI
 * ------------------------------------------------------------------------- */

/* During start-up the card only accepts a slow clock (max 400 kHz).
 * About 300 CPU cycles per half clock period gives roughly 100-150 kHz.  */
static void half_clock_delay(void)
{
	if (slow_clock) {
		for (volatile uint32_t i = 0; i < 50; i++) {
		}
	}
}

/* Send one byte and receive one byte at the same time (that is how SPI
 * works: 8 clock pulses move 8 bits out on MOSI and 8 bits in on MISO).
 * Most significant bit first.                                            */
static uint8_t spi_transfer(uint8_t out)
{
	uint8_t in = 0;
	for (uint8_t bit = 0; bit < 8; bit++) {
		// 1. Put our next bit on MOSI while the clock is low
		if (out & 0x80) {
			SD_PIO->PIO_SODR = SD_MOSI;      // Set Output Data   -> 1
		}
		else {
			SD_PIO->PIO_CODR = SD_MOSI;      // Clear Output Data -> 0
		}
		out = out << 1;
		half_clock_delay();

		// 2. Rising edge: the card reads MOSI, and we read the card's bit on MISO
		SD_PIO->PIO_SODR = SD_SCK;
		in = in << 1;
		if (SD_PIO->PIO_PDSR & SD_MISO) {    // Pin Data Status Register = pin levels
			in = in | 1;
		}
		half_clock_delay();

		// 3. Falling edge: the card puts its next bit on MISO
		SD_PIO->PIO_CODR = SD_SCK;
	}
	return in;
}

/* Faster version, only for the 512 data bytes of a sector (after start-up).
 * We only RECEIVE here, so MOSI just stays 1 (the card wants 0xFF while it
 * sends) and there is no delay: about 4 times faster than spi_transfer().
 * MISO is read at the END of the high phase, long after the card changed
 * it on the previous falling edge.                                       */
__attribute__((optimize("O2"))) static uint8_t spi_receive(void)
{
	uint32_t in = 0;
	SD_PIO->PIO_SODR = SD_MOSI;
 /* Eight identical mode-0 edges, without a branch on every received bit
  * or a loop counter. Keep the original GPIO routing and startup clock. */
#define SD_RX_BIT() do { \
 SD_PIO->PIO_SODR=SD_SCK; \
 in=(in<<1)|((SD_PIO->PIO_PDSR&SD_MISO)/SD_MISO); \
 SD_PIO->PIO_CODR=SD_SCK; \
 } while(0)
 SD_RX_BIT();SD_RX_BIT();SD_RX_BIT();SD_RX_BIT();
 SD_RX_BIT();SD_RX_BIT();SD_RX_BIT();SD_RX_BIT();
#undef SD_RX_BIT
	return (uint8_t)in;
}

static void card_select(void)
{
	SD_CS_PIO->PIO_CODR = SD_CS;  // CS = 0: the card listens
}

static void card_deselect(void)
{
	SD_CS_PIO->PIO_SODR = SD_CS;  // CS = 1
	spi_transfer(0xFF);           // 8 extra clocks: the card lets go of MISO
}

/* The card answers 0xFF when it is ready, other values while it is busy. */
static uint8_t wait_until_ready(uint32_t timeout_ms)
{
	uint32_t start = timer_millis();
	while (spi_transfer(0xFF) != 0xFF) {
		if ((uint32_t)(timer_millis() - start) > timeout_ms) {
			return 0;
		}
	}
	return 1;
}

/*
 * Send one command and return the card's first answer byte (R1).
 * The card must already be selected (card_select()).
 *
 * A command is always 6 bytes (spec section 7.3.1.1):
 *   byte 0: 01xxxxxx  = 0x40 + command number
 *   byte 1-4: 32-bit argument, most significant byte first
 *   byte 5: CRC7 + end bit 1
 * In SPI mode the CRC is only checked for CMD0 and CMD8, so only those two
 * need the right value. All other commands can send 0x01.
 *
 * The answer comes 1 to 8 bytes later. It is the first byte whose top bit
 * is 0. 0x00 = OK, 0x01 = OK but still in idle state, other bits = errors.
 */
static uint8_t send_command(uint8_t command, uint32_t argument)
{
	uint8_t crc = 0x01;
	if (command == CMD0_GO_IDLE) {
		crc = 0x95;
	}
	if (command == CMD8_SEND_IF_COND) {
		crc = 0x87;
	}

	wait_until_ready(300);

	spi_transfer(0x40 | command);
	spi_transfer((uint8_t)(argument >> 24));
	spi_transfer((uint8_t)(argument >> 16));
	spi_transfer((uint8_t)(argument >> 8));
	spi_transfer((uint8_t)(argument));
	spi_transfer(crc);

	uint8_t r1 = 0xFF;
	for (uint8_t i = 0; i < 10; i++) {
		r1 = spi_transfer(0xFF);
		if ((r1 & 0x80) == 0) {
			break;                    // a real answer (top bit 0)
		}
	}
	return r1;
}

/* ---------------------------------------------------------------------------
 *  Start-up (spec section 7.2.1, figure 7-2)
 * ------------------------------------------------------------------------- */
sd_result_t sd_init(void)
{
	/* ---- Pins -------------------------------------------------------------
	 * PMC_PCER0: clock to PIOC and PIOB. Needed to READ input pins (MISO).
	 * Write-only "command" registers (PER, SODR, CODR, OER, ODR, PUER) are
	 * written with "=": only the bits that are 1 are changed.             */
	PMC->PMC_PCER0   = (1u << SD_PIO_ID) | (1u << SD_CS_PIO_ID);

	// Chip select first: CS = 1 (card not selected) before the pin becomes an output
	SD_CS_PIO->PIO_PER  = SD_CS;
	SD_CS_PIO->PIO_SODR = SD_CS;
	SD_CS_PIO->PIO_OER  = SD_CS;

	SD_PIO->PIO_PER  = SD_SCK | SD_MOSI | SD_MISO;          // normal I/O pins
	SD_PIO->PIO_SODR = SD_MOSI;                             // MOSI = 1
	SD_PIO->PIO_CODR = SD_SCK;                              // clock idles low (SPI mode 0)
	SD_PIO->PIO_OER  = SD_SCK | SD_MOSI;                    // outputs
	SD_PIO->PIO_ODR  = SD_MISO;                             // MISO is an input ...
	SD_PIO->PIO_PUER = SD_MISO;                             // ... with pull-up (no card -> reads 0xFF)

	slow_clock = 1;
	block_addressing = 0;

	// The card needs a few ms after power-up
	uint32_t start = timer_millis();
	while ((uint32_t)(timer_millis() - start) < 10) {
	}

	/* ---- 1. At least 74 clock pulses with CS = 1 ------------------------- */
	for (uint8_t i = 0; i < 10; i++) {
		spi_transfer(0xFF);           // 10 x 8 = 80 pulses
	}

	/* ---- 2. CMD0 with CS = 0: reset, card switches to SPI mode ----------- */
	uint8_t r1 = 0xFF;
	for (uint8_t attempt = 0; attempt < 10; attempt++) {
		card_select();
		r1 = send_command(CMD0_GO_IDLE, 0);
		card_deselect();
		if (r1 == R1_IDLE) {
			break;
		}
	}
	if (r1 != R1_IDLE) {
		return SD_ERR_NO_CARD;
	}

	/* ---- 3. CMD8: argument 0x1AA = "2.7-3.6 V" + check pattern 0xAA -------
	 * SD version 2 cards answer R7: R1 + 4 bytes, the last two must be 0x01 0xAA.
	 * Old version 1 cards answer "illegal command".                       */
	uint8_t version2 = 0;
	card_select();
	r1 = send_command(CMD8_SEND_IF_COND, 0x1AA);
	if ((r1 & R1_ILLEGAL_COMMAND) == 0) {
		uint8_t r7[4];
		for (uint8_t i = 0; i < 4; i++) {
			r7[i] = spi_transfer(0xFF);
		}
		if (r7[2] != 0x01 || r7[3] != 0xAA) {
			card_deselect();
			return SD_ERR_VOLTAGE;
		}
		version2 = 1;
	}
	card_deselect();

	/* ---- 4. ACMD41 until the card is ready (R1 = 0x00), max 1 second ------
	 * ACMD = CMD55 followed by the command. Bit 30 (HCS) tells the card
	 * that we also understand SDHC/SDXC cards.                            */
	uint32_t argument = 0;
	if (version2) {
		argument = 0x40000000;
	}
	start = timer_millis();
	do {
		card_select();
		send_command(CMD55_APP_CMD, 0);
		r1 = send_command(ACMD41_SEND_OP_COND, argument);
		card_deselect();
		if ((uint32_t)(timer_millis() - start) > 1000) {
			return SD_ERR_TIMEOUT;
		}
	} while (r1 != 0x00);

	/* ---- 5. Version 2: CMD58 reads the OCR register ------------------------
	 * OCR bit 30 (CCS) = 1 -> SDHC/SDXC: the address in CMD17 is a sector
	 * number. 0 -> address in bytes.                                       */
	if (version2) {
		card_select();
		r1 = send_command(CMD58_READ_OCR, 0);
		uint8_t ocr[4];
		for (uint8_t i = 0; i < 4; i++) {
			ocr[i] = spi_transfer(0xFF);
		}
		card_deselect();
		if (r1 != 0x00) {
			return SD_ERR_OCR;
		}
		if (ocr[0] & 0x40) {
			block_addressing = 1;
		}
	}

	/* ---- 6. Old (byte-addressed) cards: make sure a block is 512 bytes ---- */
	if (!block_addressing) {
		card_select();
		r1 = send_command(CMD16_SET_BLOCKLEN, SD_SECTOR_SIZE);
		card_deselect();
		if (r1 != 0x00) {
			return SD_ERR_BLOCK_SIZE;
		}
	}

	slow_clock = 0;                   // start-up done: full speed from now on
	return SD_OK;
}

/* ---------------------------------------------------------------------------
 *  Read one sector (spec section 7.2.3, figure 7-3)
 *    CMD17 -> R1 = 0x00 -> wait for token 0xFE -> 512 data bytes -> 2 CRC bytes
 * ------------------------------------------------------------------------- */
uint8_t sd_read_sector(uint32_t sector, uint8_t *buffer)
{
	uint32_t address = sector;
	if (!block_addressing) {
		address = sector * SD_SECTOR_SIZE;    // old cards want the byte address
	}

	card_select();
	if (send_command(CMD17_READ_BLOCK, address) != 0x00) {
		card_deselect();
		return 0;
	}

	// The card sends 0xFF while it looks for the data, then the token 0xFE
	uint32_t start = timer_millis();
	uint8_t token = 0xFF;
	while (token == 0xFF) {
		token = spi_transfer(0xFF);
		if ((uint32_t)(timer_millis() - start) > 200) {
			card_deselect();
			return 0;
		}
	}
	if (token != DATA_TOKEN) {        // an error token instead of data
		card_deselect();
		return 0;
	}

	for (uint16_t i = 0; i < SD_SECTOR_SIZE; i++) {
		buffer[i] = spi_receive();
	}
	spi_receive();                    // 16-bit CRC: read but not checked
	spi_receive();

	card_deselect();
	return 1;
}

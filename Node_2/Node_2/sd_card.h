/*
 * sd_card.h
 *
 * microSD card in SPI mode (Adafruit microSD breakout, 3.3 V only).
 *
 * This layer only knows "sectors": blocks of 512 bytes numbered 0, 1, 2 ...
 * It does not know about files. Files are in fat32.c, sound is in sound.c:
 *
 *      sound.c   (WAV file -> samples -> DAC)
 *         |
 *      fat32.c   (folders and files -> sector numbers)
 *         |
 *      sd_card.c (sector number -> 512 bytes, over SPI)   <-- this file
 *
 * The SPI is done in software ("bit-banging") on four normal I/O pins, so
 * any four free pins can be used. The pins are chosen at the top of
 * sd_card.c. Reading one sector takes about 1 ms (clock ~6 MHz), and
 * 16-bit mono music at 22 050 Hz needs about 86 sectors per second, so
 * the main loop spends less than 10 % of its time on the SD card.
 *
 * Wiring (Due pin -> SD breakout pin):
 *   3.3V -> 3V      GND -> GND
 *   D46  -> CLK     D48 -> SI (DI)     D52 -> CS     D50 <- SO (DO)
 * NEVER connect the SD board to 5 V: it has no level shifters.
 */

#ifndef SD_CARD_H_
#define SD_CARD_H_

#include <stdint.h>

#define SD_SECTOR_SIZE 512

typedef enum {
    SD_OK = 0,
    SD_ERR_NO_CARD,   // no answer to CMD0 (no card, wrong wiring, no power)
    SD_ERR_VOLTAGE,   // card says it can't run at 3.3 V (CMD8)
    SD_ERR_TIMEOUT,   // card did not finish its start-up (ACMD41)
    SD_ERR_OCR,       // could not read the card type (CMD58)
    SD_ERR_BLOCK_SIZE // old card refused 512-byte blocks (CMD16)
} sd_result_t;

/* Start the card. timer_init() must have been called first (timeouts use
 * timer_millis()). Takes up to about 1 s.                                  */
sd_result_t sd_init(void);

/* Read one sector (512 bytes) into buffer. Returns 1 = OK, 0 = error.     */
uint8_t sd_read_sector(uint32_t sector, uint8_t *buffer);

#endif /* SD_CARD_H_ */

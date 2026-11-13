/*
 * sound.h
 *
 * Music player: .WAV files from the SD card -> DAC0 -> PAM8302 amplifier
 * -> speaker. Node 1 chooses the song and the volume over CAN.
 *
 *   SD card --(sd_card.c, fat32.c)--> sound_update() --> ring buffer
 *   ring buffer --> TC3_Handler (one sample, 22 050 times a second) --> DAC0
 *
 * Automatic soundtrack/SFX policy: audio.c. Required files are looked up by
 * full path in MUSIC/ and SFX/; directory order is irrelevant.
 * WAV files that work: PCM ("uncompressed"), 8, 16 or 24 bit, mono or
 * stereo, 8 000 - 48 000 Hz. Best: 16-bit mono, 22 050 Hz (small, and the
 * SD card has least work).
 *
 * Wiring:
 *   Due DAC0 --[1 kOhm]--> amp A+       Due GND --> amp A-
 *   Due 5V   ------------> amp VIN      Due GND --> amp GND
 *   speaker  ------------> green terminal (+ and -)
 *   amp SD (shutdown) pin: through an NPN transistor to D44 (see sound.c),
 *   so the amp is switched OFF (silent) when no music plays. Left open, the
 *   amp is always on.
 */

#ifndef SOUND_H_
#define SOUND_H_

#include <stdint.h>

/* CAN messages for sound (must match can.h on Node 1) */
#define CAN_ID_SOUND                                                                                       \
    0x110 // Node 1 -> Node 2, 2 bytes:
          //   [0] music Auto(1)/Off(0); SFX remains available
          //   [1] volume 0..10 (0 = silent, Node 1 sends 0 for "mute")
#define CAN_ID_SOUND_INFO                                                                                  \
    0x210 // Node 2 -> Node 1, 3 bytes:
          //   [0] number of songs on the card
          //   [1] song playing now (0 = none)
          //   [2] state (SOUND_STATE_...)
#define CAN_ID_SOUND_NAME                                                                                  \
    0x211                  // Node 2 -> Node 1, 8 bytes, the name in 4 parts:
                           //   [0] song number  [1] part 0..3
                           //   [2..7] 6 letters of the name (0 = end)
#define SOUND_NAME_PARTS 4 // 4 x 6 = 24 >= FAT32_NAME_MAX (20) letters

/* State byte in CAN_ID_SOUND_INFO */
#define SOUND_STATE_OK 0        // card read, songs found
#define SOUND_STATE_NO_CARD 1   // no SD card (or wiring problem)
#define SOUND_STATE_NOT_FAT32 2 // card is not formatted FAT32
#define SOUND_STATE_NO_SONGS 3  // no .WAV files in the main folder
#define SOUND_STATE_BAD_FILE 4  // the chosen file is not a WAV we can play
#define SOUND_STATE_READ_ERR 5  // the card stopped answering while playing

#define SOUND_MUSIC_FOLDER "MUSIC" // folder with the songs (short name, CAPITALS)
#define SOUND_MAX_SONGS 16
#define SOUND_VOLUME_MAX 10

/* Start the DAC + sample timer and read the list of songs from the card.
 * Call after timer_init(). Prints the songs on the terminal.             */
void sound_init(void);

/* Music enable and master volume/mute. Safe to repeat; state policy selects
 * files. Volume scales both voices; Off disables only music. */
void sound_set(uint8_t song, uint8_t volume);

void sound_update(void);

/* Call every main-loop pass: sends INFO / NAME to Node 1 every 50 ms.
 */
void sound_can_update(uint32_t now_ms);
void sound_report(void); // UART '?' read-only diagnostics

#endif /* SOUND_H_ */

/************************************************************************/
/*
misc:
 * Foreground: one file-sector refill per pass; a cluster boundary may also
 * read a FAT sector. Actual SD latency remains hardware-dependent.
 *
 * Also prints a warning (max once per second) if the music buffer ran
 * empty, which you hear as crackling.
 */
/************************************************************************/

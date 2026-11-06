/*
 * hof.h - HIGH SCORES: top 3 scores per game with 3-letter initials,
 *         saved in the ATmega162's EEPROM (kept when the power is off).
 *
 *   Turbo Rush : score = points
 *   Ping Pong  : score = seconds survived (game over after 3 goals)
 */
#ifndef HOF_H_
#define HOF_H_

#include <stdint.h>
#include "Menu.h"      // nav_event_t

#define HOF_TURBO     0
#define HOF_PINGPONG  1
#define HOF_GAMES     2
#define HOF_SIZE      3     // top 3 per game

void     hof_init(void);                           // call once at start-up
uint16_t hof_best(uint8_t game);                   // best score so far
uint8_t  hof_qualifies(uint8_t game, uint16_t score); // 1 = good enough for the top 3

/* Initials entry ("NEW HIGH SCORE!") */
void     hof_entry_start(uint8_t game, uint16_t score);
uint8_t  hof_entry_update(nav_event_t edge);       // edge = new joystick event (or NAV_NEUTRAL)
                                                   // returns 1 when the name is saved
/* Table screen */
void     hof_draw(uint8_t game);                   // draws the top 3 of one game

#endif /* HOF_H_ */

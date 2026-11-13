/*
 * car.c - TURBO RUSH (see car.h)
 *
 * ---------------------------------------------------------------------------
 * SCREEN LAYOUT (128 x 64 pixels = 8 pages of 8 pixel rows)
 * ---------------------------------------------------------------------------
 *   page 0      : score + best score
 *   pages 1-2   : lane 0 (16 pixels tall)
 *   pages 3-4   : lane 1
 *   pages 5-6   : lane 2
 *   page 7      : speed bar
 *
 * Every lane is exactly 2 pages tall, so a car is always 2 whole bytes high
 * and moving left/right is just "draw it one column further". No bit
 * shifting and no frame buffer are needed: each byte sent to the OLED is
 * computed on the fly from the road markings + the sprites in that lane.
 *
 * Sprites are 16 pixels tall and stored as two arrays: _top (page 1 of the
 * lane, bit 0 = top pixel) and _bot (page 2 of the lane).
 * ---------------------------------------------------------------------------
 */
#include "usart.h" // F_CPU (needed by util/delay.h)
#include "car.h"
#include "oled.h"
#include "hof.h"
#include <avr/pgmspace.h>
#include <util/delay.h>

/* spr_player: 16 x 16 pixels */
static const uint8_t spr_player_top[16] PROGMEM = { 0x80, 0xC0, 0xE0, 0xD0, 0xC8, 0xC8, 0xC8, 0xF8, 0xC8, 0xC8, 0xC8, 0xD0, 0xE0, 0xC0, 0xC0, 0x80 };
static const uint8_t spr_player_bot[16] PROGMEM = { 0x03, 0x0B, 0x17, 0x1F, 0x17, 0x0B, 0x03, 0x03, 0x03, 0x03, 0x0B, 0x17, 0x1F, 0x17, 0x0B, 0x03 };
/* spr_enemy: 16 x 16 pixels */
static const uint8_t spr_enemy_top[16] PROGMEM = { 0x80, 0x40, 0x40, 0x60, 0x50, 0x48, 0x48, 0x48, 0x78, 0x48, 0x48, 0x48, 0x50, 0x60, 0x40, 0x80 };
static const uint8_t spr_enemy_bot[16] PROGMEM = { 0x03, 0x0A, 0x16, 0x1E, 0x16, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x0A, 0x16, 0x1E, 0x16, 0x0A, 0x03 };
/* spr_truck: 24 x 16 pixels */
static const uint8_t spr_truck_top[24] PROGMEM = { 0x00, 0xF0, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xF0, 0x80, 0xFC, 0xE4, 0xE4, 0xE4, 0xFC, 0x00 };
static const uint8_t spr_truck_bot[24] PROGMEM = { 0x00, 0x17, 0x2E, 0x3E, 0x2E, 0x16, 0x06, 0x16, 0x2E, 0x3E, 0x2E, 0x16, 0x06, 0x06, 0x06, 0x06, 0x17, 0x2F, 0x3F, 0x2D, 0x17, 0x07, 0x07, 0x06 };
/* spr_coin: 10 x 16 pixels */
static const uint8_t spr_coin_top[10] PROGMEM = { 0x00, 0xC0, 0x20, 0xD0, 0x28, 0x28, 0xD0, 0x20, 0xC0, 0x00 };
static const uint8_t spr_coin_bot[10] PROGMEM = { 0x00, 0x00, 0x01, 0x02, 0x05, 0x05, 0x02, 0x01, 0x00, 0x00 };

#define LANES        3
#define PLAYER_X     4 // player car is always at the left side
#define MAX_OBJ      6 // max cars/trucks/coins on the road at the same time
#define SPEED_START  2 // pixels per frame
#define SPEED_MAX    6
#define SPEED_STEP   1500 // +1 speed every 1500 points
#define BOOST        2    // extra speed while SR5 is held

enum { OBJ_NONE, OBJ_CAR, OBJ_TRUCK, OBJ_COIN };

typedef struct {
	int16_t x;    // left edge in pixels (can be negative while leaving the screen)
	uint8_t lane; // 0..2
	uint8_t type; // OBJ_...
} obj_t;

static obj_t    obj[MAX_OBJ];
static uint8_t  player_lane;
static uint16_t score;
static uint8_t  speed;
static int16_t  spawn_timer; // pixels until the next car is added
static uint8_t  dash_offset; // moves the dashed lane lines -> looks like driving
static uint8_t  frame;
static uint16_t best; // best score from the High Scores (EEPROM)
static uint16_t rng;  // random number state

static uint16_t rnd(void)
{
	rng ^= rng << 7;
	rng ^= rng >> 9;
	rng ^= rng << 8;
	return rng;
}
/************************************************************************/
/*
 * xorshift16: a tiny random number generator (never returns 0 if rng != 0)
 */
/************************************************************************/

static uint8_t obj_width(uint8_t type)
{
	if (type == OBJ_TRUCK) return 24;
	if (type == OBJ_COIN)  return 10;
	return 16;
}

static uint8_t sprite_byte(uint8_t type, uint8_t col, uint8_t bottom)
{
	const uint8_t *top;
	const uint8_t *bot;

	// Pick the two tables of this sprite (all stored in flash)
	if (type == OBJ_CAR) {
		top = spr_enemy_top;
		bot = spr_enemy_bot;
	}
	else if (type == OBJ_TRUCK) {
		top = spr_truck_top;
		bot = spr_truck_bot;
	}
	else if (type == OBJ_COIN) {
		top = spr_coin_top;
		bot = spr_coin_bot;
	}
	else { // OBJ_NONE is used for the player car
		top = spr_player_top;
		bot = spr_player_bot;
	}

	if (bottom) {
		return pgm_read_byte(&bot[col]);
	}
	return pgm_read_byte(&top[col]);
}
/************************************************************************/
/*
 * One byte (8 vertical pixels) of a sprite. bottom = 0: top page, 1: bottom page
 */
/************************************************************************/

static void draw_lane_half(uint8_t lane, uint8_t bottom)
{
	// Make a short list of the objects in this lane first (faster loop below)
	uint8_t list[MAX_OBJ];
	uint8_t n = 0;
	for (uint8_t i = 0; i < MAX_OBJ; i++) {
		if (obj[i].type != OBJ_NONE && obj[i].lane == lane) list[n++] = i;
	}

	oled_pos(1 + 2 * lane + bottom, 0);
	oled_data_begin();
	for (uint8_t c = 0; c < 128; c++) {
		uint8_t b = 0;

		// Road: solid edge at the very top and bottom, dashed lines between lanes
		if (!bottom && lane == 0)         b |= 0x01;
		if (bottom && lane == LANES - 1)  b |= 0x80;
		if (bottom && lane < LANES - 1 && ((uint8_t)(c + dash_offset) & 15) < 8) b |= 0x80;

		// Player car
		if (lane == player_lane && c >= PLAYER_X && c < PLAYER_X + 16) {
			b |= sprite_byte(OBJ_NONE, c - PLAYER_X, bottom);
		}

		// Traffic and coins
		for (uint8_t k = 0; k < n; k++) {
			obj_t *o = &obj[list[k]];
			int16_t dx = (int16_t)c - o->x;
			if (dx >= 0 && dx < obj_width(o->type)) {
				b |= sprite_byte(o->type, (uint8_t)dx, bottom);
			}
		}
		oled_data_byte(b);
	}
	oled_data_end();
}
/************************************************************************/
/*
 * Draw one page (half a lane) over the full width.
 * For every column: start with the road markings, then OR in every sprite
 * that covers this column. The bytes are sent in one SPI burst.
 */
/************************************************************************/

static void draw_hud(void)
{
	oled_pos(0, 0);
	oled_print_P(PSTR("Score "));
	oled_print_number(score, 5);
	oled_print_P(PSTR(" Hi "));
	oled_print_number(best, 5);
	oled_clear_rest();
}
/************************************************************************/
/*
 * Top row: "Score  1234 Hi  5678" (numbers right-aligned, 5 wide)
 */
/************************************************************************/

static void draw_speed_bar(void)
{
	oled_progress_bar(7, (uint8_t)((uint16_t)speed * 100 / SPEED_MAX));
}

void car_redraw(void)
{
	oled_clear();
	draw_hud();
	for (uint8_t lane = 0; lane < LANES; lane++) {
		draw_lane_half(lane, 0);
		draw_lane_half(lane, 1);
	}
	draw_speed_bar();
}

void car_start(uint16_t seed)
{
	for (uint8_t i = 0; i < MAX_OBJ; i++) obj[i].type = OBJ_NONE;
	player_lane = 1;
	score = 0;
	speed = SPEED_START;
	spawn_timer = 40;
	dash_offset = 0;
	frame = 0;
	rng = seed;
	if (rng == 0) {
		rng = 0xACE1; // the generator must never start at 0
	}
	best = hof_best(HOF_TURBO);
	car_redraw();
}

static void spawn(void)
{
	uint8_t slot = MAX_OBJ;
	for (uint8_t i = 0; i < MAX_OBJ; i++) {
		if (obj[i].type == OBJ_NONE) { slot = i; break; }
	}
	if (slot == MAX_OBJ) return; // road is full

	uint8_t lane = rnd() % LANES;
	// 1 in 10 a coin, 2 in 10 a truck, the rest normal cars
	uint8_t r = rnd() % 10;
	uint8_t type;
	if (r == 0) {
		type = OBJ_COIN;
	}
	else if (r < 3) {
		type = OBJ_TRUCK;
	}
	else {
		type = OBJ_CAR;
	}

	// Never block all 3 lanes: count the OTHER lanes that already have
	// a car/truck near the right edge. If both are blocked, make it a coin.
	uint8_t blocked = 0;
	for (uint8_t i = 0; i < MAX_OBJ; i++) {
		if (obj[i].type == OBJ_NONE) continue;
		if (obj[i].lane == lane && obj[i].x > 128 - 32) return; // too close in the same lane
		if (obj[i].type != OBJ_COIN && obj[i].lane != lane && obj[i].x > 128 - 56) {
			blocked |= (1 << obj[i].lane);
		}
	}
	if (type != OBJ_COIN && (blocked | (1 << lane)) == 0x07) {
		type = OBJ_COIN;
	}

	obj[slot].x = 128;
	obj[slot].lane = lane;
	obj[slot].type = type;
}
/************************************************************************/
/*
 * Add a new car / truck / coin at the right edge of the screen
 */
/************************************************************************/

uint8_t car_update(int8_t lane_move, uint8_t boost)
{
	uint8_t crashed = 0;

	// ---- Player: one lane per joystick push ----------------------------
	if (lane_move < 0 && player_lane > 0)         player_lane--;
	if (lane_move > 0 && player_lane < LANES - 1) player_lane++;

	uint8_t v = speed; // pixels everything moves this frame
	if (boost) {
		v = v + BOOST;
	}

	// ---- Move everything to the left, check for hits --------------------
	for (uint8_t i = 0; i < MAX_OBJ; i++) {
		obj_t *o = &obj[i];
		if (o->type == OBJ_NONE) continue;

		o->x -= v;
		if (o->x < -24) { // left the screen
			o->type = OBJ_NONE;
			continue;
		}

		// Hit box: 2 pixels smaller on each side, so near misses are fair
		if (o->lane == player_lane &&
		    o->x + 2 < PLAYER_X + 16 - 2 &&
		    o->x + obj_width(o->type) - 2 > PLAYER_X + 2) {
			if (o->type == OBJ_COIN) {
				score += 50; // coin = bonus
				o->type = OBJ_NONE;
			}
			else {
				crashed = 1;
			}
		}
	}

	// ---- New traffic ------------------------------------------------------
	spawn_timer -= v;
	if (spawn_timer <= 0) {
		spawn();
		// Shorter gaps at higher speed -> harder
		spawn_timer = 34 + (rnd() % 40) - 3 * speed;
	}

	// ---- Score and speed ----------------------------------------------------
	score += v;
	uint8_t new_speed = SPEED_START + score / SPEED_STEP;
	if (new_speed > SPEED_MAX) new_speed = SPEED_MAX;
	if (new_speed != speed) {
		speed = new_speed;
		draw_speed_bar();
	}
	dash_offset += v;
	frame++;

	// ---- Draw: the 6 lane pages every frame, the score every 4th frame -----
	for (uint8_t lane = 0; lane < LANES; lane++) {
		draw_lane_half(lane, 0);
		draw_lane_half(lane, 1);
	}
	if ((frame & 3) == 0) draw_hud();

	return crashed;
}

void car_crash_animation(void)
{
	draw_hud();
	for (uint8_t i = 0; i < 3; i++) {
		oled_flash();
	}
	oled_clear_line(3);
	oled_clear_line(4);
	oled_print_big_centered(3, "CRASH!");
	_delay_ms(900);
}

uint16_t car_score(void)
{
	return score;
}

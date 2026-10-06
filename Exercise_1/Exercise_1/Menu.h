/*
 * Menu.h
 *
 * Created: 22.09.2026 15:07:54
 *  Author: ngabo
 */ 
#ifndef MENU_H_
#define MENU_H_

#include <stdint.h>

#define MENU_MAX_ITEMS 8

// Special sentinel value for submenus[i]: means "clicking this item
// pops back to the parent menu" instead of selecting a leaf or
// entering a real sub-menu.
#define MENU_BACK ((menu_t *)1)
typedef enum {
	NAV_NEUTRAL,
	NAV_UP,
	NAV_DOWN,
	NAV_LEFT,
	NAV_RIGHT,
	NAV_CLICK
} nav_event_t;

typedef struct menu {
	const char *title;
	const char *items[MENU_MAX_ITEMS];
	uint8_t item_count;
	struct menu *submenus[MENU_MAX_ITEMS];   // NULL = leaf item
} menu_t;

nav_event_t nav_read(void);
int8_t menu_run(menu_t *root);

#endif //MENU_H_
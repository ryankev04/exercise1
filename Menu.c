/*
 * Menu.c
 *
 * Created: 22.09.2026 15:07:40
 *  Author: ngabo
 */ 

#include "Menu.h"
#include "Io.h"
#include "Oled.h"
#include "usart.h"
#include <stdlib.h>
#include <util/delay.h>

// UNVERIFIED PLACEHOLDERS -- confirm with a raw print test
// (print j.x, j.y, j.btn while moving the stick / pressing the button)
// before trusting these:
#define NAV_CENTER    126
#define NAV_THRESHOLD 40
#define NAV_BTN_PRESSED_VALUE 1

// A small "stack" of menus, so we can go INTO a sub-menu and come
// BACK OUT of it later. depth=0 is the top-level menu.
#define MENU_STACK_DEPTH 8

// Reads the joystick right now and turns it into one simple "event":
// which direction it's pushed, or whether the button is clicked.
nav_event_t nav_read(void)
{
	io_joystick_t j = io_read_joystick();

// Button takes priority -- if it's pressed, report CLICK straight
// away and don't even look at direction.
	if (j.btn == NAV_BTN_PRESSED_VALUE) 
	{
		return NAV_CLICK;
	}

// How far off-center is the stick, on each axis?
	int16_t dev_x = (int16_t)j.x - NAV_CENTER;
	int16_t dev_y = (int16_t)j.y - NAV_CENTER;

	if (abs(dev_x) > abs(dev_y))
	 {
		if (dev_x > NAV_THRESHOLD) 
		{ 
			return NAV_RIGHT;
		}
		if (dev_x < -NAV_THRESHOLD)
		{
			 return NAV_LEFT;
		} 
		}
		else 
	{
		if (dev_y > NAV_THRESHOLD) 
		{ 
			return NAV_UP;
		}
		if (dev_y < -NAV_THRESHOLD)
		{
			 return NAV_DOWN;
		}
	}

	return NAV_NEUTRAL;
}

// Redraws the whole menu: clears the screen, then lists every item,
// putting a ">" next to whichever one is currently highlighted.
static void menu_draw(menu_t *m, uint8_t selected)
{
	oled_clear();
	for (uint8_t i = 0; i < m->item_count; i++) {
		oled_pos(i, 0);
		oled_print(i == selected ? ">" : " ");
		oled_print(m->items[i]);
	}
}

// Runs the menu until the user picks a final item, then returns which
// index they picked.
int8_t menu_run(menu_t *root)
{
	menu_t *stack[MENU_STACK_DEPTH];
	uint8_t selected_stack[MENU_STACK_DEPTH];  // which item is highlighted, per menu level
	uint8_t depth = 0;   

	stack[0] = root;
	selected_stack[0] = 0;
	menu_draw(stack[0], 0);  // show the menu before waiting for any input

	nav_event_t last_event = NAV_NEUTRAL;

	while (1) {
		nav_event_t event = nav_read();
		
		// If the SAME direction is still being held from last time,
		// ignore it -- otherwise holding "up" would scroll through
		// the whole list instantly instead of moving one step at a time.

		if (event != NAV_NEUTRAL && event == last_event) {
			last_event = event;
			_delay_ms(20);
			continue;
		}
		last_event = event;

		menu_t *current = stack[depth];  // the menu we're currently looking at
		uint8_t *sel = &selected_stack[depth]; // which item is highlighted in it

		if (event == NAV_UP) {
			// Move highlight up one; wrap to the bottom if already at the top.
			if (*sel == 0) *sel = current->item_count - 1;
			else (*sel)--;
			menu_draw(current, *sel);
		}
		else if (event == NAV_DOWN)
		 {
			 // Move highlight down one; wrap to the top if already at the bottom.
			(*sel)++;
			if (*sel >= current->item_count) *sel = 0;
			menu_draw(current, *sel);
		}
		else if (event == NAV_CLICK) {
			menu_t *sub = current->submenus[*sel];
			
			if (sub == MENU_BACK) {
				if (depth > 0) {
					depth--;
					menu_draw(stack[depth], selected_stack[depth]);
				}
			}
			else if (sub != NULL && depth < MENU_STACK_DEPTH - 1)
			 {
				// This item leads to a sub-menu -- go one level deeper.
				depth++;
				stack[depth] = sub;
				selected_stack[depth] = 0;
				menu_draw(sub, 0);
				} 
				else if (sub == NULL) 
				{
					// This item is a final choice, not a sub-menu -- we're done.
				return *sel;
			}
		}
		else if (event == NAV_LEFT && depth > 0) {
			// Go back up one menu level (only if we're not already at the top).
			depth--;
			menu_draw(stack[depth], selected_stack[depth]);
		}

		_delay_ms(20); // small pause between reads, so it doesn't run too fast
	}
}
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
#define MENU_HELP_ROW            7
#define MENU_FIRST_ITEM_ROW      1
#define MENU_VISIBLE_ITEMS       6
#define BLINK_PERIOD_TICKS      12  // 12 * 20 ms = approximately 240 ms
#define SR6_CANCEL_MASK       0x20

typedef enum {
	PENDING_NONE,
	PENDING_BACK,
	PENDING_SELECT
} pending_action_t;

/*
 * Every not-yet-implemented menu entry opens this page instead of returning
 * from menu_run(). That keeps the UI alive and provides one safe way out.
 */
static menu_t empty_menu = {
	.title = "Empty",
	.items = { "Back" },
	.item_count = 1,
	.submenus = { MENU_BACK }
};

// Convert the large analogue joystick reading into one menu event.
nav_event_t nav_read(void)
{
	io_joystick_t j = io_read_joystick();

// Button takes priority -- if it's pressed, report CLICK straight
// away and don't even look at direction.
	if (j.btn == NAV_BTN_PRESSED_VALUE) {
		return NAV_CLICK;
	}

// How far off-center is the stick, on each axis?
	int16_t dev_x = (int16_t)j.x - NAV_CENTER;
	int16_t dev_y = (int16_t)j.y - NAV_CENTER;

	if (abs(dev_x) > abs(dev_y)) {
		if (dev_x > NAV_THRESHOLD) { 
			return NAV_RIGHT;
		}
		if (dev_x < -NAV_THRESHOLD) {
			 return NAV_LEFT;
		} 
	}
	else {
		if (dev_y > NAV_THRESHOLD) { 
			return NAV_UP;
		}
		if (dev_y < -NAV_THRESHOLD) {
			 return NAV_DOWN;
		}
	}

	return NAV_NEUTRAL;
}

// First item shown when a menu contains more than six entries.
/*
* Scrolling: only 6 rows fit on screen, but a menu can hold up to
* MENU_MAX_ITEMS (8). These two helpers decide which side is visible.
*/

// First item shown when a menu contains more than six entries.
//
//  As long as the selection is within the first 6 items, show items
// 0..5. Once it goes further, scroll so the selected item sits on the
// LAST visible row.
//   selected = 0..5 -> first = 0          (shows 0..5)
//   selected = 6    -> first = 6 - 5 = 1  (shows 1..6)
//   selected = 7    -> first = 7 - 5 = 2  (shows 2..7)
static uint8_t menu_first_visible_item(uint8_t selected)
{
	if (selected < MENU_VISIBLE_ITEMS) {
		return 0;
	}
	return selected - (MENU_VISIBLE_ITEMS - 1);
}


// Which OLED row (page) the selected item is currently drawn on.
//   row = 1 (first item row) + how far the selection is from the top
//         of the visible slice.
// Example: selected = 7, first visible = 2 -> row = 1 + 7 - 2 = 6.

static uint8_t menu_selected_row(uint8_t selected)
{
	return MENU_FIRST_ITEM_ROW
	+ selected
	- menu_first_visible_item(selected);
}

static void menu_draw_help(pending_action_t pending)
{
	oled_clear_line(MENU_HELP_ROW);
	oled_pos(MENU_HELP_ROW, 0);

	if (pending == PENDING_BACK) {
		oled_print("Release:< SR6:Cancel");
	}
	else if (pending == PENDING_SELECT) {
		oled_print("Release:> SR6:Cancel");
	}
	else {
		// Exactly 21 font characters = 126 pixels on the 128-pixel display.
		oled_print("U/D Move  < Back > Go");
	}
}



// Change only the one-character marker; the item text remains untouched.
// Used for blinking: redrawing the whole screen every 240 ms would flicker
// and waste SPI time, so only column 0 of the selected row is rewritten.
static void menu_draw_marker(uint8_t selected, char marker)
{
	char text[2] = { marker, '\0' };
	oled_pos(menu_selected_row(selected), 0);
	oled_print(text);
}

// Draw title, visible menu entries, and the persistent help line.
static void menu_draw(menu_t *menu, uint8_t selected)
{
	uint8_t first = menu_first_visible_item(selected);
	
	oled_clear();
	oled_pos(0, 0);
	oled_print(menu->title);
	
	for (uint8_t row = 0; row < MENU_VISIBLE_ITEMS; row++) {
		uint8_t item = first + row;
		if (item >= menu->item_count) {
			break;
		}

		oled_pos(MENU_FIRST_ITEM_ROW + row, 0);
		oled_print(item == selected ? ">" : " ");
		oled_print(menu->items[item]);
	}
	menu_draw_help(PENDING_NONE);
}


/*
* menu_go_back(): leave the current submenu and return to its amin menu where it came from.
*
* Takes POINTERS to the stack, the selection array and depth, because it
* has to change menu_run()'s local variables (C passes by value, so without
* the pointer the change would be lost when the function returns).
*
* At depth 0 (root menu) there is nowhere to go, so it just redraws.
* selected_stack[*depth] still holds the parent's old selection, so the
* marker lands back on the item you entered from.
*/

static void menu_go_back(menu_t **stack, uint8_t *selected_stack, uint8_t *depth)
{
	if (*depth > 0) {
		(*depth)--;
	}
	menu_draw(stack[*depth], selected_stack[*depth]);
}

/*
 * Open the selected submenu. A NULL submenu is not allowed to terminate the
 * menu anymore; it opens the shared Empty page instead.
 */
static void menu_open_selected(menu_t **stack, uint8_t *selected_stack, uint8_t *depth)
{
	menu_t *current = stack[*depth];
	menu_t *sub = current->submenus[selected_stack[*depth]];

	if (sub == MENU_BACK) {
		menu_go_back(stack, selected_stack, depth);
		return;
	}

	if (*depth >= MENU_STACK_DEPTH - 1) {
		return;
	}

	(*depth)++;
	stack[*depth] = (sub == NULL) ? &empty_menu : sub;
	selected_stack[*depth] = 0;
	menu_draw(stack[*depth], 0);
}





/*
* menu_run(): the menu's main loop. Runs every ~20 ms and, each pass:
*
*   1. Reads the joystick event and the I/O-board buttons.
*   2. Updates the two LED groups .
*   3. Handles input in ONE of three modes, checked in this order:
*        a) An action is ARMED (waiting)   -> wait for release / SR6 / blink
*        b) Just CANCELLED (wait_for_neutral) -> ignore stick until centred
*        c) NORMAL navigation              -> UP/DOWN/LEFT/RIGHT/CLICK
*      Modes a) and b) end with "continue", so only one mode runs per pass.
*
* NOTE: this loop never exits -- there is no "return" inside while(1).
* So the int8_t return value is never produced, and in main.c the code
* after menu_run() (printf("Selected..."), the LED while loop) is never
* reached. If you later want e.g. "Start Game" to leave the menu, add a
* sentinel for it and "return" the selected index from here.
*/
int8_t menu_run(menu_t *root)
{
	menu_t *stack[MENU_STACK_DEPTH];
	uint8_t selected_stack[MENU_STACK_DEPTH];  // which item is highlighted, per menu level
	uint8_t depth = 0;   

	stack[0] = root;
	selected_stack[0] = 0;
	menu_draw(stack[0], 0);  // show the menu before waiting for any input

	nav_event_t last_event = NAV_NEUTRAL;
	pending_action_t pending = PENDING_NONE; // is a LEFT/RIGHT action armed
	uint8_t blink_ticks = 0; // counts loop passes between blink toggles
	uint8_t blink_visible = 1; // 1 = marker shown, 0 = marker hidden
	uint8_t wait_for_neutral = 0;  // 1 = ignore stick until it returns to centre

	while (1) {
		nav_event_t event = nav_read();
		io_buttons_t buttons = io_read_buttons();
		uint8_t sr6_pressed = (buttons.right & SR6_CANCEL_MASK) != 0;
		
		/*
		 * The large analogue joystick is physically on the SL side, so any
		 * direction/click joins the SL buttons and drives LED1..LED3.
		 * The small 5-way switch is physically on the SR side, so it joins
		 * the SR buttons and drives LED4..LED6.
		 *
		 * These I/O-board transactions finish before an OLED redraw begins;
		 * the SPI driver guarantees that only one chip select is low.
		 */
		uint8_t left_active = ((buttons.left & IO_LEFT_BUTTON_MASK) != 0) || (event != NAV_NEUTRAL);
		uint8_t right_active = ((buttons.right & IO_RIGHT_BUTTON_MASK) != 0) || ((buttons.nav & IO_NAV_BUTTON_MASK) != 0);
		io_led_groups_update(left_active, right_active);
		
				// selected is a POINTER into selected_stack[], so writing *selected
				// updates the stored selection for this level directly.
		menu_t *current = stack[depth];
		uint8_t *selected = &selected_stack[depth];
		
		/*
		 * A horizontal action has been armed. SR6 has priority over release,
		 * so pressing SR6 at the same moment as releasing still cancels.
		 */
		if (pending != PENDING_NONE) {
			if (sr6_pressed) {
				
			// Cancel: forget the armed action and restore the normal screen.
			// The stick is probably still held sideways, so wait for it to
			// be centred before accepting new input
				pending = PENDING_NONE;
				wait_for_neutral = 1;
				menu_draw(current, *selected);
			}
			else {
				
				// "Released" = the stick is no longer pushed in the armed
				// direction. (It counts as released as soon as it leaves that
				// direction -- back to neutral, or even moved up/down.)
				uint8_t released =
				(pending == PENDING_BACK   && event != NAV_LEFT)
				|| (pending == PENDING_SELECT && event != NAV_RIGHT);

				if (released) {
					pending_action_t action = pending;
					pending = PENDING_NONE;

					if (action == PENDING_BACK) {
						menu_go_back(stack, selected_stack, &depth);
					}
					else {
						menu_open_selected(stack, selected_stack, &depth);
					}
				}
				else {
					blink_ticks++;
					if (blink_ticks >= BLINK_PERIOD_TICKS) {
						blink_ticks = 0;
						blink_visible = !blink_visible;
						menu_draw_marker(*selected,
						blink_visible
						? (pending == PENDING_BACK ? '<' : '>')
						: ' ');
					}
				}
			}

			last_event = event;
			_delay_ms(20);
			continue;
		}
		
		/*
		 * After SR6 cancellation, ignore every joystick direction until the
		 * stick reaches neutral. This prevents the held direction from arming
		 * the same action again on the next loop iteration.
		 */ 
		if (wait_for_neutral) {
			if (event == NAV_NEUTRAL) {
				wait_for_neutral = 0;
			}
			last_event = event;
			_delay_ms(20);
			continue;
		}
		
		// Up/down/click remain edge-triggered, as in the original menu.
		if (event != NAV_NEUTRAL && event == last_event) {
			_delay_ms(20);
			continue;
		}
		last_event = event;

		if (event == NAV_UP) {
			if (*selected == 0) *selected = current->item_count - 1;
			else (*selected)--;
			menu_draw(current, *selected);
		}
		else if (event == NAV_DOWN) {
			(*selected)++;
			if (*selected >= current->item_count) {
				*selected = 0;
			}
			menu_draw(current, *selected);
		}
		else if (event == NAV_LEFT && depth > 0) {
			pending = PENDING_BACK;
			blink_ticks = 0;
			blink_visible = 1;
			menu_draw_marker(*selected, '<');
			menu_draw_help(pending);
		}
		else if (event == NAV_RIGHT) {
			pending = PENDING_SELECT;
			blink_ticks = 0;
			blink_visible = 1;
			menu_draw_marker(*selected, '>');
			menu_draw_help(pending);
		}
		else if (event == NAV_CLICK) {
			// The joystick push-button remains an immediate selection shortcut.
			menu_open_selected(stack, selected_stack, &depth);
		}
		
		_delay_ms(20); // small pause between reads, so it doesn't run too fast
	}
}
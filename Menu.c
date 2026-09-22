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
static uint8_t menu_first_visible_item(uint8_t selected)
{
	if (selected < MENU_VISIBLE_ITEMS) {
		return 0;
	}
	return selected - (MENU_VISIBLE_ITEMS - 1);
}

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

int8_t menu_run(menu_t *root)
{
	menu_t *stack[MENU_STACK_DEPTH];
	uint8_t selected_stack[MENU_STACK_DEPTH];  // which item is highlighted, per menu level
	uint8_t depth = 0;   

	stack[0] = root;
	selected_stack[0] = 0;
	menu_draw(stack[0], 0);  // show the menu before waiting for any input

	nav_event_t last_event = NAV_NEUTRAL;
	pending_action_t pending = PENDING_NONE;
	uint8_t blink_ticks = 0;
	uint8_t blink_visible = 1;
	uint8_t wait_for_neutral = 0;

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
		
		menu_t *current = stack[depth];
		uint8_t *selected = &selected_stack[depth];
		
		/*
		 * A horizontal action has been armed. SR6 has priority over release,
		 * so pressing SR6 at the same moment as releasing still cancels.
		 */
		if (pending != PENDING_NONE) {
			if (sr6_pressed) {
				pending = PENDING_NONE;
				wait_for_neutral = 1;
				menu_draw(current, *selected);
			}
			else {
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
/*
 * ADC.h
 *
 * Created: 14.09.2026 11:31:58
 *  Author: ngabo
 */ 

#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>
#define ADC_ADDR 0x1000   // anywhere between 0x1000 to 0x17FF
#define ADC_BASE ((volatile uint8_t *)0x1000)

#define PAD_X 0
#define PAD_Y 1
#define JOY_Y 2
#define JOY_X 3
#define joy_pad_centered 52
#define joy_pad_threshold 5 // how far it moves from the center 



typedef struct {
	uint8_t x;   // -100 .. +100, centered on calibrated resting position
	uint8_t y;
	uint8_t slid_x;
	uint8_t slid_y;
} joy_pos_t;

typedef enum {
	JOY_NEUTRAL,
	JOY_LEFT,
	JOY_RIGHT,
	JOY_UP,
	JOY_DOWN
} joystick_dir_t;


void adc_clk_init(void);
void joystick_calibrate(void);
void adc_convert_all(void);

joy_pos_t joy_slider_read(void);
joystick_dir_t joy_dir (void);
extern uint8_t adc_read_ch(uint8_t ch);

#endif //ADC_H_
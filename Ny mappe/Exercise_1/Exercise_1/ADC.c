/*
 * ADC.c
 *
 * Created: 14.09.2026 11:31:42
 *  Author: ngabo
 */ 
#include "usart.h"
#include "ADC.h"
#include <util/delay.h>
#include <stdlib.h>

static uint8_t center_x = 0;
static uint8_t center_y = 0;
static uint8_t calibrated = 0;

/*
* adc_clk(): Sets up Timer/Counter1 to generate a continuous square-wave clock signal
* on pin PD5, to feed the MAX155/MAX156 ADC's CLK input (the ADC has no clock of its own,
* it needs the AVR to supply one).

* How it works, in plain terms:
*   The timer counts up on every clock tick. Every time it reaches a certain number
*   (OCR1A), two things happen: it flips the PD5 pin's voltage (high<->low), and it
*   resets back to 0 and starts counting again. Repeating this forever produces a
*   steady square wave.

* Datasheet reference: ATmega162 datasheet, "16-bit Timer/Counter1",
*   TCCR1A p.128, WGM mode table p.130, TCCR1B/prescaler p.131-132, OCR1A p.133,
*   CTC frequency formula p.119.
*/

void adc_clk_init(void)
{
// inputs. This line flips PD5 to be an OUTPUT, so the timer hardware is actually
// allowed to drive a voltage onto it (without this, nothing would appear on the pin
// even if the timer is running correctly internally).
	DDRD |= (1 << PD5);

// COM1A0 tells the timer: "every time you hit a compare match, TOGGLE the PD5 pin
// (flip it from high to low, or low to high)". This is what actually produces the
// square wave on the pin -- without this bit set, the timer would count silently
// with no visible output.
	TCCR1A |= (1 << COM1A0);
	
// WGM12 selects "CTC mode" (Clear Timer on Compare Match). In this mode, the timer
// counts up from 0, and the moment it reaches the value stored in OCR1A, it resets
// back to 0 and starts over. This is what lets OCR1A control the frequency directly.
		
	TCCR1B |= (1 << WGM12);
	
// CS10 turns the timer ON and tells it to count using the full CPU clock speed,
// with no slowdown (no "prescaler" dividing it down). Without setting any CS bit,
// the timer stays stopped and never counts at all.
	
	TCCR1B |= (1 << CS10);


// OCR1A is the "count up to this number, then reset" value. This number directly
// sets the output frequency. The formula (from the datasheet, page 119) is:
//
//     frequency = F_CPU / (2 * N * (OCR1A + 1))
//
// where N is the prescaler (=1 here, since we chose CS10 with no division above).
// With F_CPU = 4,915,200 Hz and OCR1A = 1:
//     frequency = 4,915,200 / (2 * 1 * (1+1)) = 4,915,200 / 4 ? 1.23 MHz
	OCR1A = 1;
}

void adc_convert_all(void)
{
	*ADC_BASE = 0x00;
	_delay_us(100);
}

uint8_t adc_read_ch(uint8_t ch)
{
	uint8_t value = 0;
	for (uint8_t i = 0; i <= ch; i++) 
	{
		value = *ADC_BASE;
	}
	
	return value;
}


void joystick_calibrate(void)
{
	printf("Calibrating joystick -- leave it centered...\n");
	_delay_ms(500);

	adc_convert_all();
	center_x = adc_read_ch(JOY_X);
	center_y = adc_read_ch(JOY_Y);
	calibrated = 1;

	//printf("Calibration done: center_x=%d center_y=%d\n", center_x, center_y);
}




int8_t to_percent(uint8_t input, uint8_t min, uint8_t max)
{
	int16_t numerator = ((int16_t)input - (int16_t)min)*100;
	int16_t dinominator = ((int16_t)max - (int16_t)min);
	int16_t percent = (numerator / dinominator);
	return (uint8_t)percent;
}

joy_pos_t joy_slider_read(void)
{
	if (!calibrated)
	{
		printf("WARNING CHECK YOUR CALIBRATION");
	}
		adc_convert_all();
		uint8_t raw_x = adc_read_ch(JOY_X);
		uint8_t raw_y = adc_read_ch(JOY_Y);
		
		uint8_t raw_slid_x = adc_read_ch(PAD_X);
		uint8_t raw_slid_y = adc_read_ch(PAD_Y);
	joy_pos_t pos;
	pos.x= to_percent(raw_x , 69 , 241);
	pos.y= to_percent(raw_y, 67, 242);
	pos.slid_x = to_percent(raw_slid_x , 69 , 241);
	pos.slid_y = to_percent(raw_slid_y , 69 , 241);
	//printf("  per_x %2d | per_y %2d | per_pad_x %2d | per_pad_y %2d \r\n", pos.x, pos.y, pos.slid_x, pos.slid_y );
	//_delay_ms(300);
	return pos;
}



joystick_dir_t joy_dir (void)
{
	joy_pos_t pos = joy_slider_read();
	int8_t dev_x = pos.x - joy_pad_centered;
	int8_t dev_y =  pos.y - joy_pad_centered;
	//int8_t dev_pad_x = pos.slid_x - joy_pad_centered;
	//int8_t dev_pad_y = pos.slid_y - joy_pad_centered;
	
	if(abs(dev_x) > abs(dev_y))
	{
		if (dev_x > joy_pad_threshold)
		{
			return JOY_RIGHT;
		}
		if (dev_x < -joy_pad_threshold)
		{
			return JOY_LEFT;
		}
	}
		else
		{
			if (dev_y > joy_pad_threshold)
			{
				return JOY_UP;
			}
			if (dev_y < -joy_pad_threshold)
			{
				return JOY_DOWN;
			}
		}
		
	
	return JOY_NEUTRAL;
}



/*

*/



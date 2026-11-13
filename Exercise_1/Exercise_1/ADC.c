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

static uint8_t center_x = 0;   // Stores the raw X reading taken during calibration.
static uint8_t center_y = 0;   // Stores the raw Y reading taken during calibration.
static uint8_t calibrated = 0; // Set after calibration to suppress the warning.


void adc_clk_init(void)
{
	// PD5 is the OC1A (Output Compare A) pin of Timer1.
	// Set PD5 as an output so the timer can drive its clock
	// signal onto the physical pin.
	DDRD |= (1 << PD5);

	// COM1A0 enables toggling of OC1A on every compare match
	// (assuming COM1A1 = 0 and non-PWM mode).
	// Each match switches PD5 between HIGH and LOW,
	// producing a square-wave clock signal.
	TCCR1A |= (1 << COM1A0);
	
	// WGM12 selects CTC mode (Clear Timer on Compare Match),
	// assuming the other WGM bits are zero (WGM13:0 = 0100).
	// Timer1 counts from 0 up to OCR1A (TOP), then clears
	// the counter on the following timer clock.
	// OCR1A therefore determines the compare-match interval.
	TCCR1B |= (1 << WGM12);
	
	// CS10 selects the timer clock without prescaling,
	// assuming CS12:CS11 = 00 (CS12:0 = 001).
	// Timer1 now counts at clk_I/O, without dividing the clock.
	TCCR1B |= (1 << CS10);


	// OCR1A (Output Compare Register 1A) is a 16-bit register
	// that can hold values from 0 to 65535.
	//
	// In CTC mode, OCR1A defines TOP: the counter value at
	// which a compare match occurs.
	//
	// Changing OCR1A controls how frequently compare matches occur:
	//   - Smaller value -> fewer timer ticks -> higher output frequency.
	//   - Larger value  -> more timer ticks  -> lower output frequency.
	//   - OCR1A = 0    -> maximum output frequency (F_CPU / 2N).
	//
	// OCR1A = 1 produces a 1.2288 MHz clock with F_CPU = 4.9152 MHz
	// and prescaler N = 1.
	//
	// Reference: ATmega162 datasheet, pp. 119, 133.
	OCR1A = 1;
}
/************************************************************************/
/*
 * adc_clk_init(): Generates the external clock required by the
 * MAX155/MAX156 ADC using ATmega162 Timer/Counter1.
 *
 * Timer1 operates in CTC mode, using OCR1A as TOP. Each compare match
 * toggles the OC1A output (PD5), producing a continuous square wave.
 * 
 * Output frequency:
 *     f_OC1A = F_CPU / (2 * N * (1 + OCR1A)) 
 * With F_CPU = 4.9152 MHz, N = 1 and OCR1A = 1:
 *     f_OC1A = 1.2288 MHz
 *
 * References: ATmega162 datasheet, pp. 119, 128-132.
 */
/************************************************************************/


void adc_convert_all(void){
	*ADC_BASE = 0x00;
	_delay_us(100);
}
/************************************************************************/
/*
 * adc_convert_all(): Starts one conversion sequence for the inputs
 * selected by the ADC's configuration.
 *
 * Writing to ADC_BASE generates a write pulse on the external memory
 * bus. In hard-wired mode, the ADC ignores the byte value and uses
 * the pulse to trigger the sequence. The inputs are sampled together,
 * then converted one at a time. The fixed 100 us delay allows time
 * for conversion to finish without polling BUSY.
 *
 * Reference: MAX155/MAX156 datasheet, pp. 8, 14, 17.
 */
/************************************************************************/


uint8_t adc_read_ch(uint8_t ch)
{
	uint8_t value = 0;
	for (uint8_t i = 0; i <= ch; i++){
		value = *ADC_BASE;
	}
	
	return value;
}
/************************************************************************/
/*
 * adc_read_ch(): Reads ch + 1 results from the ADC's current read
 * position and returns the last one.
 *
 * Each volatile access to ADC_BASE generates a read pulse and
 * advances the ADC's result counter. With all four MAX156 inputs
 * configured for single-ended conversion, the results start at
 * channel 0 and proceed in ascending order. In that setup, ch
 * corresponds to the channel number only if this call starts at
 * the first result of a fresh conversion.
 *
 * Reference: MAX155/MAX156 datasheet, pp. 8, 10, 14-15.
 */
/************************************************************************/


void joystick_calibrate(void){
	printf("Calibrating joystick -- leave it centered...\n");
	_delay_ms(500);

	adc_convert_all();
	center_x = adc_read_ch(JOY_X);
	center_y = adc_read_ch(JOY_Y);
	calibrated = 1;

}
/************************************************************************/
/*
 * joystick_calibrate(): Prompts the user to center the joystick,
 * waits 500 ms, and attempts to capture one raw count per axis.
 *
 * Both reads follow the same conversion, so the Y read starts
 * wherever the X read left the ADC's result counter. The calibrated
 * flag records that the routine ran. The position and direction
 * functions below do not use the stored center counts.
 */
/************************************************************************/


int8_t to_percent(uint8_t input, uint8_t min, uint8_t max)
{
	int16_t numerator = ((int16_t)input - (int16_t)min)*100;
	int16_t dinominator = ((int16_t)max - (int16_t)min);
	int16_t percent = (numerator / dinominator);
	return (uint8_t)percent;
}
/************************************************************************/
/*
 * to_percent(): Scales a raw count using two distinct endpoints.
 *
 * The integer calculation maps min to 0 and max to 100 and
 * truncates fractional results toward zero. Inputs outside the
 * endpoint range can produce a quotient below 0 or above 100.
 * The return expression converts the quotient to uint8_t before
 * the function returns it as int8_t.
 */
/************************************************************************/


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
	return pos;
}
/************************************************************************/
/*
 * joy_slider_read(): Starts one ADC conversion sequence and scales
 * the values returned by the four read calls using fixed endpoints.
 *
 * Each call continues from the ADC's current read position. If
 * calibration has not run, the function prints a warning and
 * continues with the same reads and scaling.
 */
/************************************************************************/


joystick_dir_t joy_dir (void)
{
	joy_pos_t pos = joy_slider_read();
	int8_t dev_x = pos.x - joy_pad_centered;
	int8_t dev_y =  pos.y - joy_pad_centered;
	// int8_t dev_pad_x = pos.slid_x - joy_pad_centered;
	// int8_t dev_pad_y = pos.slid_y - joy_pad_centered;
	
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
/************************************************************************/
/*
 * joy_dir(): Uses the sign and magnitude of the joystick's deviation
 * from joy_pad_centered to return a direction.
 *
 * The axis with the larger absolute deviation determines direction.
 * Equal deviations select Y. The selected deviation must exceed the
 * threshold in either direction, so a value exactly at the threshold
 * returns JOY_NEUTRAL. The commented-out lines would compute the same
 * kind of deviation for the sliders, which do not affect direction.
 */
/************************************************************************/




/************************************************************************/
/*
misc:
 *
 * printf("Calibration done: center_x=%d center_y=%d\n", center_x, center_y);
 *
 * printf("  per_x %2d | per_y %2d | per_pad_x %2d | per_pad_y %2d \r\n", pos.x, pos.y, pos.slid_x, pos.slid_y );
 */
/************************************************************************/

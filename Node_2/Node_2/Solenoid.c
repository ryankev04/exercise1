/*
 * Solenoid.c
 *
 * Created: 03/11/2026 12:41:27
 *  Author: muval
 */ 

#include "sam.h"
#include "Solenoid.h"

#define SOLENOID_PIN       (1u << 22) // PC22 mask for the active-low relay drive.
#define SOLENOID_PULSE_MS  40u // Elapsed time before the timer switches the drive off.

static volatile uint32_t pulse_start_ms = 0; // Timestamp of the last accepted pulse.
static volatile uint8_t solenoid_active = 0; // Set while a pulse is in progress.



void Solenoid_init(void){
	PMC->PMC_PCER0 = (1u << ID_PIOC);

	PIOC->PIO_SODR = SOLENOID_PIN; // Set the output latch high before enabling the pin.

	// Enable GPIO control and configure as output
	PIOC->PIO_PER = SOLENOID_PIN;
	PIOC->PIO_OER = SOLENOID_PIN;
}
/************************************************************************/
/*
 * Solenoid_init(): Enables the Port C clock and sets PC22 up as a
 * GPIO output. Loads the inactive high level before enabling output.
 */
/************************************************************************/

void Solenoid_stop(void){
	PIOC->PIO_SODR = SOLENOID_PIN; // Active-low drive: high turns it off.
	solenoid_active = 0;
}
/************************************************************************/
/*
 * Solenoid_stop(): Ends any pulse by driving PC22 high and clearing
 * the active flag.
 */
/************************************************************************/

void Solenoid_run(uint32_t now_ms){
	if (solenoid_active) return;

	pulse_start_ms = now_ms;
	solenoid_active = 1;

	PIOC->PIO_CODR = SOLENOID_PIN; // LOW = transistor ON, relay activated
}
/************************************************************************/
/*
 * Solenoid_run(): Starts a pulse at now_ms by driving PC22 low.
 * A request received during an active pulse is ignored and does
 * not restart its timer.
 */
/************************************************************************/


void Solenoid_update(uint32_t now_ms){
	if (solenoid_active && (uint32_t)(now_ms - pulse_start_ms) >= SOLENOID_PULSE_MS){
		PIOC->PIO_SODR = SOLENOID_PIN; // HIGH = transistor OFF
		solenoid_active = 0;
	}
}
/************************************************************************/
/*
 * Solenoid_update(): Called by the 1 ms timer interrupt. Turns
 * the drive off once the active pulse reaches SOLENOID_PULSE_MS.
 * Unsigned timestamp subtraction handles millisecond counter wrap.
 */
/************************************************************************/







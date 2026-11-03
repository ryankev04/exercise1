/*
 * Solenoid.c
 *
 * Created: 03/11/2026 12:41:27
 *  Author: muval
 */ 

#include "sam.h"
#include "Solenoid.h"

#define SOLENOID_PIN       (1u << 22)
#define SOLENOID_PULSE_MS  40u

static volatile uint32_t pulse_start_ms = 0;
static volatile uint8_t solenoid_active = 0;



void Solenoid_init(void){
	PMC->PMC_PCER0 = (1u << ID_PIOC);

	// PNP transistor: HIGH = OFF
	PIOC->PIO_SODR = SOLENOID_PIN;

	// Enable GPIO control and configure as output
	PIOC->PIO_PER = SOLENOID_PIN;
	PIOC->PIO_OER = SOLENOID_PIN;
}

void Solenoid_stop(void){
	/* HIGH = OFF in your existing circuit. */
	PIOC->PIO_SODR = SOLENOID_PIN;
	solenoid_active = 0;
}

void Solenoid_run(uint32_t now_ms){
	if (solenoid_active) return;

	pulse_start_ms = now_ms;
	solenoid_active = 1;

	// LOW = transistor ON, relay activated
	PIOC->PIO_CODR = SOLENOID_PIN;
}


void Solenoid_update(uint32_t now_ms){
	if (solenoid_active && (uint32_t)(now_ms - pulse_start_ms) >= SOLENOID_PULSE_MS){
		// HIGH = transistor OFF
		PIOC->PIO_SODR = SOLENOID_PIN;
		solenoid_active = 0;
	}
}







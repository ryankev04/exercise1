/*
 * Solenoid.c
 *
 * Created: 03/11/2026 12:41:27
 *  Author: muval
 */ 

#include "sam.h"
#include "Solenoid.h"
#include "audio.h"

#define SOLENOID_PIN       (1u << 22)
#define SOLENOID_PULSE_MS  40u

static volatile uint32_t pulse_start_ms = 0;
static volatile uint8_t solenoid_active = 0;



void Solenoid_init(void){
	PMC->PMC_PCER0 = (1u << ID_PIOC);

	PIOC->PIO_SODR = SOLENOID_PIN; // PNP transistor: HIGH = OFF

	// Enable GPIO control and configure as output
	PIOC->PIO_PER = SOLENOID_PIN;
	PIOC->PIO_OER = SOLENOID_PIN;
}

void Solenoid_stop(void){
	PIOC->PIO_SODR = SOLENOID_PIN; // HIGH = OFF in your existing circuit.
	solenoid_active = 0;
}

void Solenoid_run(uint32_t now_ms){
	if (solenoid_active) return;

	pulse_start_ms = now_ms;
	solenoid_active = 1;

	PIOC->PIO_CODR = SOLENOID_PIN; // LOW = transistor ON, relay activated
    audio_effect(FX_HIT); // queue SFX only after an accepted physical command
}


void Solenoid_update(uint32_t now_ms){
	if (solenoid_active && (uint32_t)(now_ms - pulse_start_ms) >= SOLENOID_PULSE_MS){
		PIOC->PIO_SODR = SOLENOID_PIN; // HIGH = transistor OFF
		solenoid_active = 0;
	}
}







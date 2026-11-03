/*
 * Solenoid.h
 *
 * Created: 03/11/2026 12:41:40
 *  Author: muval
 */ 


#ifndef SOLENOID_H_
#define SOLENOID_H_


void Solenoid_init(void);
void Solenoid_run(uint32_t now_ms);
void Solenoid_update(uint32_t now_ms);

// typedef struct {
// 	uint8_t right;   // [0 0 R6 R5 R4 R3 R2 R1]
// 	uint8_t left;     // [0 L7 L6 L5 L4 L3 L2 L1]
// 	uint8_t nav;      // [0 0 0 Up Down Left Right Btn]
// } io_buttons_t; //For information.


#endif /* SOLENOID_H_ */
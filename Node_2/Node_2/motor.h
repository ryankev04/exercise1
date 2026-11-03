/*
 * motor.h
 *
 * Created: 26.10.2026 22:23:35
 *  Author: ngabo
 */ 

#ifndef MOTOR_H_
#define MOTOR_H_
#include <stdint.h>

void motor_init (void);
void motor_set(uint8_t dir, uint32_t duty);
void motor_set_from_joystick(uint8_t x);
void motor_safe_off(void);
#endif //MOTOR_H_
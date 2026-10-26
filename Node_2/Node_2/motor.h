/*
 * motor.h
 *
 * Created: 26.10.2026 22:23:35
 *  Author: ngabo
 */ 

#ifndef MOTOR_H_
#define MOTOR_H_

void motor_init (void);
void motor_set(uint8_t dir, uint32_t duty);
void motor_set_from_joystick(uint8_t x);
#endif //MOTOR_H_
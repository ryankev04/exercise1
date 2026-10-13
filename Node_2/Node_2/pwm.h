/*
 * pwm.h
 *
 * Created: 13.10.2026 12:16:10
 *  Author: ngabo
 */ 

#ifndef PWM_H_
#define PWM_H_

#include <stdint.h>

#define SERVO_CPRD         52500   /* 20 ms   = 0.020  s * 2 625 000 */
#define SERVO_CDTY_MIN     2363    /* 0.9 ms  = 0.0009 s * 2 625 000 = 2362.5, rounded UP   */
#define SERVO_CDTY_CENTER  3937    /* 1.5 ms  = 0.0015 s * 2 625 000 = 3937.5               */
#define SERVO_CDTY_MAX     5512    /* 2.1 ms  = 0.0021 s * 2 625 000 = 5512.5, rounded DOWN */

void servo_init(void);                     
void servo_set_cdty(uint32_t cdty);        
void servo_set_from_joystick(uint8_t x);  
uint32_t servo_get_cdty(void);
/* Joystick X after the noise filter, for printing/debugging */
uint8_t servo_get_filtered_x(void);

#endif  //PWM_H_
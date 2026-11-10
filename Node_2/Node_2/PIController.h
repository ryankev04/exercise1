/*
 * PIController.h
 *
 * Created: 27/10/2026 19:30:14
 *  Author: muval
 */ 


#ifndef PICONTROLLER_H_
#define PICONTROLLER_H_

#include <stdint.h>

#define CONTROL_PERIOD_MS 10u
#define CONTROL_MAX_DUTY 1400 // 4200 / 3: 50% cap. This is the maximum permitted motor effort in PWM counts. 
								//So if this value were to be increased, it can increase speed if the PI controller requests it. 
							

void PIControl_init(void);
void PIControl_command(uint8_t command);
void PIControl_joystick(uint8_t x, uint32_t now);
void PIControl_tick(uint32_t now);
void PIControl_print(void);



#endif /* PICONTROLLER_H_ */
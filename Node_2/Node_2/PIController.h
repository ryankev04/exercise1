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
                              // So if this value were to be increased, it can increase speed if the PI controller requests it.
							

void PIControl_init(void);
void PIControl_command(uint8_t command);
void PIControl_joystick(uint8_t x, uint32_t now);
void PIControl_tick(uint32_t now);
void PIControl_print(void);

/* Status codes sent to Node 1 (byte 0 of the status message, CAN_ID_STATUS) */
#define PI_STATUS_IDLE          0 // not calibrated, motor off
#define PI_STATUS_CALIBRATING   1 // homing: driving slowly to the left end
#define PI_STATUS_VERIFYING     2 // backoff: checking the motor moves the right way
#define PI_STATUS_POSITION      3 // playing, position control (PI)
#define PI_STATUS_SPEED         4 // playing, speed control
#define PI_STATUS_READY         5 // calibrated, motor off (menu or paused)
#define PI_STATUS_FAILED        6 // calibration / motor check failed

void PIControl_start_game(uint8_t speed_mode); // calibrate, then play (0 = position, 1 = speed)
void PIControl_pause(void);                    // motor off, keep calibration
void PIControl_resume(uint8_t speed_mode);     // continue after a pause
void PIControl_stop_game(void);                // motor off, back to the menu
uint8_t PIControl_status(void);                // one of PI_STATUS_...



#endif /* PICONTROLLER_H_ */
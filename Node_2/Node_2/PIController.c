/*
 * PIController.c
 *
 * Created: 27/10/2026 19:29:59
 *  Author: muval
 *
 * Controls carriage position using encoder feedback.
 *
 * The controller compares:
 *     target position - measured position = position error
 */ 

#include "Encoder.h"
#include "motor.h"
#include "PIController.h"
#include "sam.h"
#include <stdio.h>
#include "timer.h"


#define TRAVEL 5609 //This was the maximum displacement from one edge to the other edge.
#define MARGIN 25 //So that it doesn't crash to the edges of the playfield. N is the marginal buffer inside each endpoint.
#define LINK_TIMEOUT_MS 5000u //Controller becomes disarmed/idle when it has received nothing for N milliseconds.

#define BACKOFF_POSITION    3000		// After calibrating origin, carriage moves a buffer rightward from the left endpoint. 
#define BACKOFF_TIMEOUT_MS  2500u	

#define KP 1.00f //Proportionality gain. "f" makes this a float. 
#define KI 0.80f //Integral gain
#define DT 0.010f //Controller sampling interval

#define TARGET_STEP 80 //How far the PIController's target can change during one update. 
							//Makes the target move faster, not the carriage itself. Increasing can lead to larger position error in consequence.
#define NUDGE_UNIT 1000
#define JOG_NUDGE_UNIT	5000

static volatile uint8_t mode;		//operating state, bool check, 
static volatile uint8_t calibrated; //bool check, 1 after the operator captures the left endpoint as origin.
static volatile uint8_t verified;	//bool check, 1 after the operator confirms the motor direction is correct.
static volatile uint8_t have_rx;	//bool check, 1 after receiving at least one valid joystick CAN message.
static volatile int8_t polarity;	//+1: use current direction interpretation. -1: reverse it. For debugging purposes.
static volatile uint32_t last_rx;	//Millisecond timestamp of the latest valid joystick message.
static volatile uint32_t jog_end;	//Millisecond deadline at which a brief test movement must end.
static volatile int32_t origin;		//Raw encoder reading captured at the left endpoint.
static volatile int32_t requested;	//Desired position calculated from joystick X. Meaning, joystick x directly maps the carriage position.
static volatile int32_t target;		//Position currently pursued by the controller.
static volatile int32_t output;		//Signed, limited motor effort in PWM counts.
static float integral;				//Accumulated integral contribution to motor effort. Has units of PWM counts.

static volatile uint32_t homing_start_ms;			//
static volatile uint32_t homing_last_progress_ms;	//
static volatile int32_t homing_start_count;			//
static volatile int32_t homing_progress_count;		//

static volatile uint32_t backoff_start_ms;

static volatile uint8_t stop_reason = 0;
// 0 = none
// 1 = CAN timeout
// 2 = position outside limits
// 3 = manual stop
// 4 = CAN hasn't received message. 

enum{
	STOPPED,	//Disarmed
	JOG,		//Brief motor-direction test with an automatic deadline
	HOMING,		//Autonomous motor management. Used during calibration. 
	BACKOFF,	//Autonomous motor management. Used during calibration. 
	ACTIVE		//Controller is running.
};

static int32_t clamp(int32_t x, int32_t low, int32_t high){
	return x < low ? low : (x > high ? high : x);
}
/************************************************************************/
/*
clamp(int32_t x, int32_t low, int32_t high);
	* If "low" was the floor and "high" was the ceiling, this function makes sure that "x" is within the bounded range. If x is outside this bounded range, return floor or ceiling.

*/
/************************************************************************/

static void stop(void) {
	mode = STOPPED; 
	output = 0;		
	integral = 0;	
	motor_set(0,0); //Ask the motor driver for zero PWM duty. motor_set(direction, duty). 
					//		Direction is either 0 or 1, which will drive the motor in one direction or the opposite direction respectively.
}
/************************************************************************/
/* 
stop();
	* Disarm the motor controller and clear the accumulated effort.
*/
/************************************************************************/


static void drive(int32_t effort){
	output=clamp(effort, -CONTROL_MAX_DUTY, CONTROL_MAX_DUTY);
	effort = output*polarity;
	motor_set(effort >= 0,											//Comparison whether effort is positive (or 0) or negative.
				(uint32_t)((effort < 0) ? -effort : effort));	//This is |effort|. If it's negative, flip the sign so that motor_set takes in a positive.
}
/************************************************************************/
/* 
drive(int32_t PI_output);
	* Translates PI controller's output into physical motor commands.

*/
/************************************************************************/


void PIControl_init(void) {
	mode = STOPPED; 
	calibrated = verified = have_rx = 0; //Calibration and any saved values are cleared upon initialization. 
	polarity = -1; //Direction.
	origin = 0; //no left reference has been captured yet. calibration = 0 prevents treating this initial value as a completed calibration.
	requested = target = TRAVEL/2;
	integral = 0;
	output = 0;
	motor_set(0,0); 
}
/************************************************************************/
/*
PIControl_init();
	* PI startup. Establish starting state.
	* Clear any calibration data or remembered effort. 

*/
/************************************************************************/


void PIControl_joystick(uint8_t x, uint32_t now_ms){
	int32_t new_pos; //New requested carriage position in encoder counts.
	
	x = (uint8_t)clamp(x, 54, 201); //Clamp x to the full range of motion that the joystick can move along the x.
	if (x >= 124 && x <= 128) new_pos = TRAVEL/2; //Often when the joystick is idle, we don't want the motor to oscillate back and forth. 
											//		So we request a predictable, stable carriage position for these joystick readings.
	else if (x < 124) new_pos = MARGIN + (int32_t)(x-54)*(TRAVEL/2 - MARGIN)/70; //If joystick is tilted leftward, aka x is from 54 to 117, 
																				//		encoder counts will range from 50 (LEFT MARGIN) to around 2460. Mapped.
																				//126 - 54 = 72. Dividing by 72 normalizes how far the joystick tilts through the left half. 
	else new_pos = TRAVEL/2 + (int32_t)(x-128)*(TRAVEL-MARGIN - TRAVEL/2)/73; //If joystick is tilted rightward, aka x is from 135 to 201, 
																							//		encoder counts will range from around 3135 to 5559 (RIGHT MARGIN)
																							//201 - 126 = 75. Dividing by 75 normalizes how far the joystick tilts through the right half.
	
	new_pos = clamp(new_pos, MARGIN, TRAVEL-MARGIN); //If joystick x mapping ever changes that causes new_pos to exceed the margins, 
														//			this defensive programming measure will prevent the motor from flying. 
	
	requested = new_pos;	//Store the new requested position
	last_rx = now_ms;		//Store when the message arrived.
	have_rx = 1;			//Confirm received message. If any of these three steps have a significant delay, like an interrupt, that can cause issues!
	

}
/************************************************************************/
/*
PIControl_joystick(uint8_t x, uint32_t now_ms);
	* Translate joystick's horizontal position into a desired carriage position, which the PI controller will attempt to reach.

*/
/************************************************************************/


void PIControl_command(uint8_t c){
	uint32_t now_ms = timer_millis();
	if (c == 'S' || c == 's' || c == ' '){ stop(); stop_reason = 2;} //S for "Stop".
	else if ((c == 'C' || c == 'c') && mode == STOPPED){ //C for "Calibrate the current position".
		calibrated = 0;
		homing_start_ms = homing_last_progress_ms = now_ms;
		homing_start_count = homing_progress_count = encoder_read();		
		mode = HOMING; //PIControl_tick will notice this change and continue the calibration procedure.
		drive(-NUDGE_UNIT);
	}
	else if ((c == 'R' || c == 'r') && mode == STOPPED){ polarity = -polarity; verified = 0;} //R for "Reverse polarity".
	else if ((c == 'V' || c == 'v') && mode == STOPPED) verified = 1; //V for "Verified".
	else if ((c == ',' || c == '.') && mode == STOPPED){ //Test motor effort, comma for left, period for right. 
		mode = JOG;
		jog_end = now_ms + 100u; //This test lasts for 100 milliseconds.
		drive(c=='.' ? JOG_NUDGE_UNIT: -JOG_NUDGE_UNIT); //Period? Nudge the motor rightward, otherwise leftward.
	}
	else if ((c == 'A' || c == 'a') && mode == STOPPED && calibrated && verified && have_rx && (uint32_t)(now_ms - last_rx) <= LINK_TIMEOUT_MS){ //A for "Activate".
		int32_t pos = encoder_read() - origin;
		if (pos >= MARGIN && pos <= TRAVEL-MARGIN){
			target = pos;
			integral = 0;
			mode = ACTIVE;
		}
	}
	else if (c == 'E' || c == 'e'){
		printf("Encoder Count: %d\nNormalized Count: %d\n", (int)encoder_read(), (int)(encoder_read() - origin));
	}
}
/************************************************************************/
/*
PIControl_command(uint8_t c);
	* Command control to communicate with the PI Controller thereby the motor. 
	* S for STOP.
	* P for Print values. 
Only when the PI Controller is stopped:
	* C for Calibrate 
	* R for Reverse Polarity
	* V for Verified.
	* , and . to test the motor left and right. 
	* A for Activate.

*/
/************************************************************************/



void PIControl_tick(uint32_t tick_now){
	if (mode == HOMING){
		int32_t count = encoder_read();

		if (count <= homing_progress_count - 3){     // still moving left
			homing_progress_count = count;
			homing_last_progress_ms = tick_now;
		}
		if ((uint32_t)(tick_now - homing_last_progress_ms) >= 300){
			drive(0);
			origin = count;
			backoff_start_ms = tick_now;
			mode = BACKOFF;
			return;
		}
		if ((uint32_t)(tick_now - homing_start_ms) > 5000){
			drive(0);
			calibrated = 0;
			mode = STOPPED;
			return;
		}
		return;
	}
	if (mode == BACKOFF){
		int32_t pos = encoder_read() - origin;
		if (pos >= BACKOFF_POSITION){
			stop();
			calibrated = (pos <= TRAVEL - MARGIN); //As long as the position is within bounds, the carriage is calibrated. 
			return;
		}
		if ((uint32_t)(tick_now - backoff_start_ms) >= BACKOFF_TIMEOUT_MS // If time elapsed is greater than timeout,
		|| pos < -MARGIN){													//or position is out of bounds.
			calibrated = 0;
			stop();
			return;
		}
		drive(NUDGE_UNIT);
		return;
	}
	int32_t pos = encoder_read() - origin; // Position measured relative to origin (relative to left endpoint, if calibrated).
	if (mode == JOG){
		if ((int32_t)(tick_now - jog_end) >= 0) stop(); //If a test is being run, it will have a timer deadline. "tick_now" is the elapsed time, and if it elapses beyond "jog_end", stop() executes.
		return; //If the test is indeed over, don't bother executing control_tick, get out of here.
	}
	if (mode == STOPPED) return;
	if ((uint32_t)(tick_now - last_rx) > LINK_TIMEOUT_MS){		//If the latest message is more than 500 ms old,
		stop();													//...do not continue. 
		stop_reason = 1;										//Provide reason for the stop. 
		return;
	}
	if (!have_rx){											//If we have not received a valid joystick CAN message,
		stop();													//...do not continue.
		stop_reason = 4;										//Provide reason for the stop.
		return;
	}
	if (pos < -50										//or the position is more than 50 counts left of the reference, basically if the carriage is out-of-bounds.
	|| pos > TRAVEL+50){								//or the position is more than 50 counts beyond the expected right endpoint, basically if the carriage is out-of-bounds.)
		stop_reason = 2;
		stop();
		return;
	}
	int32_t delta = clamp(requested-target, -TARGET_STEP, TARGET_STEP); //Requested = position selected by the joystick. Target = position the controller currently pursues.
																			//The difference between the two tells us how far the target needs to move. 
																			//clamp() limits that change to +/-TARGET_STEP per update.
	target += delta;
	int32_t error = target - pos; //Positive error: carriage needs to move towards increasing counts. Negative error: decreasing counts.
	
	float I_effort_temp = integral+KI*DT*(float)error;	//Microcontrollers does not continuously evaluate integrals. 
														//So approximate the area contributed during one interval as a small rectangle and sum up.
														//This is "temp" because we have not accepted it into "integral" yet. This is a proposed new candidate.
	float PI_output_temp = KP*(float)error + I_effort_temp; //Predict the total PI effort if we accepted the candidate.
	if ((PI_output_temp <= CONTROL_MAX_DUTY && PI_output_temp >= -CONTROL_MAX_DUTY)		//Accept the candidate if: The proposed effort fits within the motor effort limits.
		|| (PI_output_temp > CONTROL_MAX_DUTY && error < 0)								//or candidate exceeds the positive limit but error is negative, therefore this update reduces the integral. 
		|| (PI_output_temp < -CONTROL_MAX_DUTY && error > 0)) {integral = I_effort_temp;}//or candidate exceeds the negative limit but error is positive, therefore this update reduces the integral. 

// 	float PI_output_f = KP*(float)error + integral; //If float version is necessary.
	int32_t PI_output = (int32_t)(KP*(float)error + integral);
	
	if((pos <= MARGIN && PI_output < 0)				//If position reaches the left boundary, and PI controller requests further leftward movement,
		|| (pos >= TRAVEL-MARGIN && PI_output > 0)){	//or if position reaches the right boundary, and PI controller requests further rightward movement, 
		PI_output = 0;								//...the motor receives zero duty.
		integral = 0;
	}
	drive(PI_output);
}

/************************************************************************/
/*
control_tick();
	* Perform one controller update. Called by the timer interrupt every N ms. 
	* This function runs in an interrupt, so do not print or wait inside here.

*/
/************************************************************************/

void PIControl_print(void){
	printf("PIControl_print: || Mode: %u || Calibrated?: %u || Polarity: %d || Verified?: %u || Raw Encoder Value: %d || Origin: %d || Position: %d || Target Position: %d || Requested Position: %d || PI Output: %d\n", 
			(unsigned)mode, (unsigned)calibrated, (int)polarity, (unsigned)verified, (int)encoder_read(), (int)origin, (int)(encoder_read() - origin), (int)target, (int)requested, (int)output);
	if (!(stop_reason == 0)){
		printf("STOP_REASON=%u ; CAN_AGE=%lu ms\r\n", stop_reason, (unsigned long)(timer_millis() - last_rx));
	}
}

/************************************************************************/
/*
PIControl_print();
	* Print everything we know from this .c file :)
*/
/************************************************************************/




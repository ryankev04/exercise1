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


#define TRAVEL 5609           // Nominal end-to-end travel in encoder counts.
#define MARGIN 25             // Position-control margin inside each endpoint, in counts.
#define LINK_TIMEOUT_MS 5000u // Maximum joystick frame age in active control.

#define BACKOFF_POSITION    3000 // Backoff target relative to the captured origin, in counts.
#define BACKOFF_TIMEOUT_MS  2500u // Time allowed to reach the backoff target.	

#define KP 0.50f  // Proportional gain.
#define KI 0.80f  // Integral gain
#define DT 0.010f // Controller update interval in seconds.

#define TARGET_STEP 80 // Maximum target change per update, in encoder counts.
#define NUDGE_UNIT 1000 // Requested PWM counts for homing and backoff.
#define JOG_NUDGE_UNIT	5000 // Requested jog effort before drive() clamps it.

static volatile uint8_t mode;       // Current state from the enum below.
static volatile uint8_t calibrated; // Set after successful homing and backoff.
static volatile uint8_t verified;   // Direction verification flag for manual activation.
static volatile uint8_t have_rx;    // Set after the first valid joystick frame.
static volatile int8_t polarity;    // +1: use current direction interpretation. -1: reverse it. For debugging purposes.
static volatile uint32_t last_rx;   // Millisecond timestamp of the latest valid joystick message.
static volatile uint32_t jog_end;   // Millisecond deadline at which a brief test movement must end.
static volatile int32_t origin;     // Raw encoder reading captured at the left endpoint.
static volatile int32_t requested;  // Desired position calculated from joystick X. Meaning, joystick x directly maps the carriage position.
static volatile int32_t target;     // Position currently pursued by the controller.
static volatile int32_t output;     // Signed, limited motor effort in PWM counts.
static float integral;              // Accumulated integral contribution to motor effort. Has units of PWM counts.

static volatile uint32_t homing_start_ms;
static volatile uint32_t homing_last_progress_ms;
static volatile int32_t homing_start_count;
static volatile int32_t homing_progress_count;

static volatile uint32_t backoff_start_ms;

static volatile uint8_t stop_reason = 0;

/************************************************************************/
/*
 * ---- Game control from Node 1 (menu "Ping Pong") ----------------------
 */
/************************************************************************/
static volatile uint8_t auto_activate = 0; // after calibration: 0 = stay stopped, 1 = POSITION, 2 = SPEED
static volatile uint8_t calib_failed = 0;  // 1 = the last calibration did not succeed
static volatile uint8_t joy_x = 126;       // newest raw joystick X, used in SPEED mode

#define SPEED_DEADBAND  10 // joystick noise around the centre must not move the motor
#define SPEED_JOY_RANGE 75 // biggest distance from the centre (201 - 126)
#define SPEED_END_MARGIN 400 // speed mode: no driving towards an end closer than this
                             // (bigger than MARGIN: without PI the carriage coasts a bit)

enum{
	STOPPED, // Disarmed
	JOG,     // Brief motor-direction test with an automatic deadline
	HOMING,  // Autonomous motor management. Used during calibration.
	BACKOFF, // Autonomous motor management. Used during calibration.
	ACTIVE,  // Controller is running (position control: joystick = where the carriage should be).
	SPEED    // Speed control: joystick = how fast and which way (no PI, like exercise task 3).
};

static int32_t clamp(int32_t x, int32_t low, int32_t high){
	return x < low ? low : (x > high ? high : x);
}
/************************************************************************/
/*
 * clamp(): Returns x limited to the inclusive interval low..high.
 */
/************************************************************************/

static void stop(void) {
	mode = STOPPED; 
	output = 0;		
	integral = 0;	
	motor_set(0,0); // Ask the motor driver for zero PWM duty. motor_set(direction, duty).
	                // Direction is either 0 or 1, which will drive the motor in one direction or the opposite direction respectively.
}
/************************************************************************/
/*
 * stop(): Disarms control, clears the effort and integral, and
 * requests zero PWM duty.
 */
/************************************************************************/

static void drive(int32_t effort){
	output=clamp(effort, -CONTROL_MAX_DUTY, CONTROL_MAX_DUTY);
	effort = output*polarity;
	motor_set(effort >= 0, // Comparison whether effort is positive (or 0) or negative.
				(uint32_t)((effort < 0) ? -effort : effort)); // This is |effort|. If it's negative, flip the sign so that motor_set takes in a positive.
}
/************************************************************************/
/*
 * drive(): Limits signed effort to CONTROL_MAX_DUTY, then applies
 * the configured polarity. The sign selects PHASE and the magnitude
 * sets the requested PWM duty.
 */
/************************************************************************/

void PIControl_init(void) {
	mode = STOPPED; 
	calibrated = verified = have_rx = 0; // Start without calibration, verification, or joystick input.
	polarity = -1;                       // Direction.
	origin = 0;                          // no left reference has been captured yet. calibration = 0 prevents treating this initial value as a completed calibration.
	requested = target = TRAVEL/2;
	integral = 0;
	output = 0;
	motor_set(0,0); 
}
/************************************************************************/
/*
 * PIControl_init(): Starts disarmed with zero duty, default polarity,
 * and a midpoint target. Calibration and joystick input are cleared.
 */
/************************************************************************/

void PIControl_joystick(uint8_t x, uint32_t now_ms){
	int32_t new_pos; // New requested carriage position in encoder counts.
	
	x = (uint8_t)clamp(x, 54, 201); // Clamp x to the full range of motion that the joystick can move along the x.
	if (x >= 124 && x <= 128) new_pos = TRAVEL/2; // Often when the joystick is idle, we don't want the motor to oscillate back and forth.
	                                              // So we request a predictable, stable carriage position for these joystick readings.
	else if (x < 124) new_pos = MARGIN + (int32_t)(x-54)*(TRAVEL/2 - MARGIN)/70; // Scale X = 54..123 toward the midpoint.
	else new_pos = TRAVEL/2 + (int32_t)(x-128)*(TRAVEL-MARGIN - TRAVEL/2)/73; // Scale X = 129..201 toward the right margin.
	
	new_pos = clamp(new_pos, MARGIN, TRAVEL-MARGIN); // Keep the requested position within the travel margins.
	
	requested = new_pos; // Store the new requested position
	joy_x = x;           // Raw (clamped) X for SPEED mode
	last_rx = now_ms;    // Store when the message arrived.
	have_rx = 1;         // Mark the joystick input as available.
	

}
/************************************************************************/
/*
 * PIControl_joystick(): Clamps X to 54..201 and maps it to a position
 * inside the track margins. X values 124..128 request the midpoint.
 * Also saves X for speed mode and refreshes the CAN input timestamp.
 */
/************************************************************************/

void PIControl_command(uint8_t c){
	uint32_t now_ms = timer_millis();
	if (c == 'S' || c == 's' || c == ' '){ stop(); stop_reason = 2;} // S for "Stop".
	else if ((c == 'C' || c == 'c') && mode == STOPPED){ // C for "Calibrate the current position".
		calibrated = 0;
		homing_start_ms = homing_last_progress_ms = now_ms;
		homing_start_count = homing_progress_count = encoder_read();		
		mode = HOMING; // PIControl_tick will notice this change and continue the calibration procedure.
		drive(-NUDGE_UNIT);
	}
	else if ((c == 'R' || c == 'r') && mode == STOPPED){ polarity = -polarity; verified = 0;} // R for "Reverse polarity".
	else if ((c == 'V' || c == 'v') && mode == STOPPED) verified = 1; // V for "Verified".
	else if ((c == ',' || c == '.') && mode == STOPPED){ // Test motor effort, comma for left, period for right.
		mode = JOG;
		jog_end = now_ms + 100u; // This test lasts for 100 milliseconds.
		drive(c=='.' ? JOG_NUDGE_UNIT: -JOG_NUDGE_UNIT); // Period? Nudge the motor rightward, otherwise leftward.
	}
	else if ((c == 'A' || c == 'a') && mode == STOPPED && calibrated && verified && have_rx && (uint32_t)(now_ms - last_rx) <= LINK_TIMEOUT_MS){ // A for "Activate".
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
 * PIControl_command(): Handles UART commands. S or space stops the
 * motor, and E prints encoder counts. While stopped, C starts homing,
 * R reverses polarity, V marks direction verified, and comma or period
 * requests a 100 ms jog. A activates position control if calibration,
 * verification, recent CAN input, and the position checks all pass.
 */
/************************************************************************/

void PIControl_tick(uint32_t tick_now){
	if (mode == HOMING){
		int32_t count = encoder_read();

		if (count <= homing_progress_count - 3){ // still moving left
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
			calib_failed = 1;  // Tell Node 1 the calibration failed
			auto_activate = 0; // ...and never start the motor by itself
			mode = STOPPED;
			return;
		}
		return;
	}
	if (mode == BACKOFF){
		int32_t pos = encoder_read() - origin;
		if (pos >= BACKOFF_POSITION){
			stop();
			calibrated = (pos <= TRAVEL - MARGIN); // As long as the position is within bounds, the carriage is calibrated.
			if (!calibrated) {
				calib_failed = 1;
			}
			else if (auto_activate == 1) { // Game started from Node 1 in POSITION mode: go by itself
				target = pos;
				integral = 0;
				mode = ACTIVE;
			}
			else if (auto_activate == 2) { // Game started from Node 1 in SPEED mode
				mode = SPEED;
			}
			auto_activate = 0;
			return;
		}
		if ((uint32_t)(tick_now - backoff_start_ms) >= BACKOFF_TIMEOUT_MS // If time elapsed is greater than timeout,
		|| pos < -MARGIN){ // or position is out of bounds.
			calibrated = 0;
			calib_failed = 1; // Moving the wrong way or not at all: the motor check failed
			auto_activate = 0;
			stop();
			return;
		}
		drive(NUDGE_UNIT);
		return;
	}
	int32_t pos = encoder_read() - origin; // Position measured relative to origin (relative to left endpoint, if calibrated).
	if (mode == JOG){
		if ((int32_t)(tick_now - jog_end) >= 0) stop(); // If a test is being run, it will have a timer deadline. "tick_now" is the elapsed time, and if it elapses beyond "jog_end", stop() executes.
		return;                                         // If the test is indeed over, don't bother executing control_tick, get out of here.
	}
	if (mode == STOPPED) return;
	if (!have_rx){ // If we have not received a valid joystick CAN message,
		stop();          // ...do not continue.
		stop_reason = 4; // Provide reason for the stop.
		return;
	}
	if ((uint32_t)(tick_now - last_rx) > LINK_TIMEOUT_MS){ // Stop if the latest frame is older than LINK_TIMEOUT_MS (5 s).
		stop();          // ...do not continue.
		stop_reason = 1; // Provide reason for the stop.
		return;
	}
	if (pos < -50        // or the position is more than 50 counts left of the reference, basically if the carriage is out-of-bounds.
	|| pos > TRAVEL+50){ // or the position is more than 50 counts beyond the expected right endpoint, basically if the carriage is out-of-bounds.)
		stop_reason = 2;
		stop();
		return;
	}
	/*
	 * SPEED mode: the joystick sets speed and direction directly (open loop),
	 * but the encoder is still used as a safety limit near both ends.
	 * X = 126 gives zero effort. Before endpoint limiting, X = 201
	 * requests the positive duty cap and X = 54 requests 96% of the
	 * negative cap.
	 */
	if (mode == SPEED){
		int32_t diff = (int32_t)joy_x - 126;
		if (diff > -SPEED_DEADBAND && diff < SPEED_DEADBAND) {
			diff = 0;
		}
		int32_t effort = diff * CONTROL_MAX_DUTY / SPEED_JOY_RANGE; // Multiply first, then divide
		if ((pos <= SPEED_END_MARGIN && effort < 0) || (pos >= TRAVEL-SPEED_END_MARGIN && effort > 0)) {
			effort = 0; // Do not drive further into an end of the track
		}
		drive(effort);
		return;
	}

	int32_t delta = clamp(requested-target, -TARGET_STEP, TARGET_STEP); // Requested = position selected by the joystick. Target = position the controller currently pursues.
	                                                                    // The difference between the two tells us how far the target needs to move.
	                                                                    // clamp() limits that change to +/-TARGET_STEP per update.
	target += delta;
	int32_t error = target - pos; // Positive error: carriage needs to move towards increasing counts. Negative error: decreasing counts.
	
	float I_effort_temp = integral+KI*DT*(float)error; // Microcontrollers does not continuously evaluate integrals.
	                                                   // So approximate the area contributed during one interval as a small rectangle and sum up.
	                                                   // This is "temp" because we have not accepted it into "integral" yet. This is a proposed new candidate.
	float PI_output_temp = KP*(float)error + I_effort_temp; // Predict the total PI effort if we accepted the candidate.
	if ((PI_output_temp <= CONTROL_MAX_DUTY && PI_output_temp >= -CONTROL_MAX_DUTY) // Accept the candidate if: The proposed effort fits within the motor effort limits.
		|| (PI_output_temp > CONTROL_MAX_DUTY && error < 0) // or candidate exceeds the positive limit but error is negative, therefore this update reduces the integral.
		|| (PI_output_temp < -CONTROL_MAX_DUTY && error > 0)) {integral = I_effort_temp;} // or candidate exceeds the negative limit but error is positive, therefore this update reduces the integral.

	int32_t PI_output = (int32_t)(KP*(float)error + integral);
	
	if((pos <= MARGIN && PI_output < 0) // If position reaches the left boundary, and PI controller requests further leftward movement,
		|| (pos >= TRAVEL-MARGIN && PI_output > 0)){ // or if position reaches the right boundary, and PI controller requests further rightward movement,
		PI_output = 0;                               // ...the motor receives zero duty.
		integral = 0;
	}
	drive(PI_output);
}
/************************************************************************/
/*
 * PIControl_tick(): Runs every 10 ms from the timer interrupt.
 * Homing treats 300 ms without sufficient leftward encoder progress
 * as the origin. Backoff moves away from it and either finishes
 * calibration or stops with a failure.
 *
 * In active modes, checks CAN age and position bounds. Speed mode
 * maps X directly to PWM effort with encoder limits near the ends.
 * Position mode limits each target step, applies PI feedback, and
 * accepts integral updates only within the effort limits or when
 * they move effort back toward those limits.
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
 * PIControl_print(): Reports calibration, encoder position, target,
 * and effort over UART. If a stop reason is recorded, also prints
 * the reason code and age of the last joystick frame.
 */
/************************************************************************/

/************************************************************************/
/*
 * Game control, called from main.c when Node 1 changes the game state.
 * These replace typing C / V / A in the terminal.
 */
/************************************************************************/

void PIControl_start_game(uint8_t speed_mode)
{
	stop();       // From any state to STOPPED ('C' needs STOPPED)
	verified = 1; // Game start marks direction verified without changing polarity.
	calib_failed = 0;
	auto_activate = speed_mode ? 2 : 1;
	PIControl_command('C'); // Start homing; PIControl_tick() does the rest
}
/************************************************************************/
/*
 * PIControl_start_game(): Stops the current motion and starts homing.
 * Successful backoff automatically enters position control for zero
 * speed_mode or open-loop speed control for a nonzero value.
 */
/************************************************************************/

void PIControl_pause(void)
{
	auto_activate = 0;
	stop();
}
/************************************************************************/
/*
 * PIControl_pause(): Stops the motor and cancels automatic activation,
 * retaining the calibration state.
 */
/************************************************************************/

void PIControl_resume(uint8_t speed_mode)
{
	if (!calibrated) { // Should not happen, but then calibrate again
		PIControl_start_game(speed_mode);
		return;
	}
	if (mode != STOPPED) {
		return; // Already running
	}
	if (speed_mode) {
		mode = SPEED;
	}
	else {
		int32_t pos = encoder_read() - origin;
		target = clamp(pos, MARGIN, TRAVEL-MARGIN);
		integral = 0;
		mode = ACTIVE;
	}
}
/************************************************************************/
/*
 * PIControl_resume(): Restarts calibration if needed. With valid
 * calibration, resumes only from STOPPED. Position mode starts from
 * the measured position clamped to the travel margins.
 */
/************************************************************************/

void PIControl_stop_game(void)
{
	auto_activate = 0;
	stop();
}
/************************************************************************/
/*
 * PIControl_stop_game(): Stops the motor and cancels automatic
 * activation when the game ends.
 */
/************************************************************************/

uint8_t PIControl_status(void)
{
	switch (mode) {
		case HOMING:  return PI_STATUS_CALIBRATING;
		case BACKOFF: return PI_STATUS_VERIFYING;
		case ACTIVE:  return PI_STATUS_POSITION;
		case SPEED:   return PI_STATUS_SPEED;
		default:      break;
	}
	if (calib_failed) return PI_STATUS_FAILED;
	if (calibrated)   return PI_STATUS_READY;
	return PI_STATUS_IDLE;
}
/************************************************************************/
/*
 * PIControl_status(): Returns the state code sent to Node 1. Active
 * control and calibration phases take precedence, followed by the
 * calibration failure flag, ready state, or idle state.
 */
/************************************************************************/

/************************************************************************/
/*
misc:
 *
 * 0 = none
 * 1 = CAN timeout
 * 2 = position outside limits or manual stop
 * 4 = CAN hasn't received message.
 *
 * float PI_output_f = KP*(float)error + integral; //If float version is necessary.
 */
/************************************************************************/

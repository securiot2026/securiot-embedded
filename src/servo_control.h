#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "servo_motion.h"

// Attaches the pan, tilt and door-lock Servo objects to their GPIO pins
// (see docs/PINOUT.md) and centers pan/tilt. Call once from setup().
void servosInit();

// Applies an incremental pan/tilt command from the Edge API's per-frame
// pan_delta/tilt_delta (each in [-1.0, 1.0]). This only sets a target
// (target = clamp(target + delta*10, 0, 180)); servoTick() performs the motion.
void applyPanTilt(float panDelta, float tiltDelta);

// Steps the pan/tilt motion profile at 50 Hz and writes a servo only when its
// integer degree changes. Call at the top of every loop() iteration.
// Non-blocking.
void servoTick();

// Opens (0 deg) or closes (180 deg) the door through the same smooth ramp as
// pan/tilt. DoorState::Unknown is ignored. Non-blocking: this only sets the
// target, the motion happens in servoTick(). The door servo is never written
// at boot; the first command writes its own angle directly, later ones ramp.
void doorSetState(DoorState state);

// Legacy alias of doorSetState(DoorState::Close), kept for the Edge rule
// door_action "lock" and the door_lock manual command.
void triggerDoorLock();

#endif // SERVO_CONTROL_H

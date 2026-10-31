#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

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

// Sweeps the door-lock servo to its locked angle and holds it there.
// MVP scope: this does not implement an "unlock" path, HW-04 only requires
// the automatic-response simulation, not a full access-control cycle.
void triggerDoorLock();

#endif // SERVO_CONTROL_H

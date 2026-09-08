#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

// Attaches the pan, tilt and door-lock Servo objects to their GPIO pins
// (see docs/PINOUT.md) and centers pan/tilt. Call once from setup().
void servosInit();

// Applies an incremental pan/tilt move from the Edge API's per-frame
// pan_delta/tilt_delta (each in [-1.0, 1.0]). This is a simple proportional
// step added to the current angle every frame, not an absolute reposition,
// matching the no-PID MVP design in CONTEXT.md.
void applyPanTilt(float panDelta, float tiltDelta);

// Sweeps the door-lock servo to its locked angle and holds it there.
// MVP scope: this does not implement an "unlock" path, HW-04 only requires
// the automatic-response simulation, not a full access-control cycle.
void triggerDoorLock();

#endif // SERVO_CONTROL_H

#ifndef SERVO_MOTION_H
#define SERVO_MOTION_H

// Arduino-free pan/tilt motion profile. servo_control.cpp is thin hardware
// glue over this header so the math can be tested on the host
// (`pio test -e native`).
//
// Profile (one ServoMotion per axis, stepped every kServoTickMs = 20 ms):
//   v_des = k * (target - theta)                 proportional pull to target
//   a_cmd = clamp(v_des - v, -a_max*dt, +a_max*dt)  acceleration limit
//   v     = clamp(v + a_cmd, -v_max, +v_max)        velocity cap
//   theta = clamp(theta + v*dt, 0, 180)             mechanical limits
//   anti-hunting: |target - theta| < 0.25 deg and |v| < 2 deg/s snaps to
//   target with v = 0.
//
// While acceleration is not saturated the update reduces to
// theta[n+1] = (1 - k*dt)*theta[n] + k*dt*target with k*dt = 0.16, a pole at
// 0.84: stable, monotone, no overshoot. Overshoot can only come from
// acceleration saturation while braking from v_max. Braking from v_max needs
// v_max^2 / (2*a_max) degrees, which must stay below v_max/k = 11.25 deg, so
// a_max must exceed k*v_max/2 = 360 deg/s^2.
//
// Tuned default a_max = 400 deg/s^2 (the first proposal was 360). 360 sits
// exactly on that stability boundary with zero margin and a sweep of steps
// 1..180 deg showed 0.83 deg of overshoot at a 134 deg step. 400 gives zero
// overshoot for every step 1..180 and for chained retargets.
//
// The latest command wins: servoMotionCommand only moves the target and the
// profile redirects from the current theta and v without a jump.
// Nothing here blocks and nothing here touches an Arduino symbol.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static const float kServoMinDeg = 0.0f;
static const float kServoMaxDeg = 180.0f;
// Degrees moved per full-scale (+/-1.0) delta from the Edge API.
static const float kServoMaxStepDegrees = 10.0f;
static const uint32_t kServoTickMs = 20u;
static const float kServoTickDt = 0.02f;
static const float kServoGainPerSec = 8.0f;
static const float kServoMaxAccelDegPerSec2 = 400.0f;
static const float kServoMaxVelDegPerSec = 90.0f;
static const float kServoSettleDistDeg = 0.25f;
static const float kServoSettleVelDegPerSec = 2.0f;
static const uint32_t kServoMaxCatchUpTicks = 5u;

struct ServoMotion {
    float theta;  // current angle, degrees
    float v;      // current velocity, degrees per second
    float target; // commanded angle, degrees
    int written;  // last integer degree reported by servoMotionPendingWrite
};

inline float servoClampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

inline void servoMotionInit(ServoMotion &m, float angle) {
    float a = servoClampf(angle, kServoMinDeg, kServoMaxDeg);
    m.theta = a;
    m.target = a;
    m.v = 0.0f;
    m.written = (int)(a + 0.5f);
}

// Adds delta*kServoMaxStepDegrees to the target. NaN and infinite deltas are
// ignored. The target stays within 0..180.
inline void servoMotionCommand(ServoMotion &m, float delta) {
    if (!isfinite(delta)) return;
    m.target = servoClampf(m.target + delta * kServoMaxStepDegrees, kServoMinDeg, kServoMaxDeg);
}

// Exactly one 20 ms tick.
inline void servoMotionStep(ServoMotion &m) {
    const float vDes = kServoGainPerSec * (m.target - m.theta);
    const float aLim = kServoMaxAccelDegPerSec2 * kServoTickDt;
    const float aCmd = servoClampf(vDes - m.v, -aLim, aLim);
    m.v = servoClampf(m.v + aCmd, -kServoMaxVelDegPerSec, kServoMaxVelDegPerSec);
    m.theta = servoClampf(m.theta + m.v * kServoTickDt, kServoMinDeg, kServoMaxDeg);

    if (fabsf(m.target - m.theta) < kServoSettleDistDeg && fabsf(m.v) < kServoSettleVelDegPerSec) {
        m.theta = m.target;
        m.v = 0.0f;
    }
}

// True (and outDeg set) only when the rounded integer degree changed since
// the last report. theta is never negative, so +0.5 and truncation rounds.
inline bool servoMotionPendingWrite(ServoMotion &m, int &outDeg) {
    const int deg = (int)(m.theta + 0.5f);
    if (deg == m.written) return false;
    m.written = deg;
    outDeg = deg;
    return true;
}

// Number of 20 ms ticks to run now. Wrap-safe: elapsed is an unsigned
// subtraction. A long gap (a blocking POST) is capped and resynchronised so
// the backlog is not replayed as a visible jump.
inline uint32_t servoClockSteps(uint32_t &lastMs, uint32_t nowMs) {
    const uint32_t elapsed = (uint32_t)(nowMs - lastMs);
    const uint32_t steps = elapsed / kServoTickMs;
    if (steps == 0u) return 0u;
    if (steps > kServoMaxCatchUpTicks) {
        lastMs = nowMs;
        return kServoMaxCatchUpTicks;
    }
    lastMs = (uint32_t)(lastMs + steps * kServoTickMs);
    return steps;
}

#endif // SERVO_MOTION_H

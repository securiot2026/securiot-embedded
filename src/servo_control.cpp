#include "servo_control.h"

#include <Arduino.h>
#include <ESP32Servo.h>

namespace {

// GPIO assignments, must stay in sync with docs/PINOUT.md and must never
// collide with a camera pin from camera_pins.h.
const int kPanServoPin = 13;
const int kTiltServoPin = 15;
const int kDoorServoPin = 14;

const int kServoMinAngle = 0;
const int kServoMaxAngle = 180;
const int kServoCenterAngle = 90;
const int kDoorLockedAngle = 180;

// Maximum degrees moved per frame for a full-scale (+/-1.0) delta. Keeps
// pan/tilt tracking smooth instead of slamming to an extreme on one frame.
const float kMaxStepDegrees = 10.0f;

Servo g_panServo;
Servo g_tiltServo;
Servo g_doorServo;

float g_panAngle = kServoCenterAngle;
float g_tiltAngle = kServoCenterAngle;

float clampAngle(float angle) {
    if (angle < kServoMinAngle) return kServoMinAngle;
    if (angle > kServoMaxAngle) return kServoMaxAngle;
    return angle;
}

} // namespace

void servosInit() {
    // Allocate all 4 LEDC timers before attaching, per ESP32Servo's own
    // Sweep example, then attach each servo at the standard 50 Hz period.
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    g_panServo.setPeriodHertz(50);
    g_tiltServo.setPeriodHertz(50);
    g_doorServo.setPeriodHertz(50);

    g_panServo.attach(kPanServoPin);
    g_tiltServo.attach(kTiltServoPin);
    g_doorServo.attach(kDoorServoPin);

    g_panAngle = kServoCenterAngle;
    g_tiltAngle = kServoCenterAngle;
    g_panServo.write((int)g_panAngle);
    g_tiltServo.write((int)g_tiltAngle);
}

void applyPanTilt(float panDelta, float tiltDelta) {
    g_panAngle = clampAngle(g_panAngle + panDelta * kMaxStepDegrees);
    g_tiltAngle = clampAngle(g_tiltAngle + tiltDelta * kMaxStepDegrees);

    g_panServo.write((int)g_panAngle);
    g_tiltServo.write((int)g_tiltAngle);
}

void triggerDoorLock() {
    g_doorServo.write(kDoorLockedAngle);
}

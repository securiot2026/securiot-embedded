#include "servo_control.h"

#include <Arduino.h>
#include <ESP32Servo.h>

#include "servo_motion.h"

namespace {

// GPIO assignments, must stay in sync with docs/PINOUT.md and must never
// collide with a camera pin from camera_pins.h.
const int kPanServoPin = 13;
const int kTiltServoPin = 15;
const int kDoorServoPin = 14;

const int kServoCenterAngle = 90;

Servo g_panServo;
Servo g_tiltServo;
Servo g_doorServo;

// Motion profile state per axis (math lives in servo_motion.h).
ServoMotion g_pan;
ServoMotion g_tilt;
DoorMotion g_door;  // never written until the first door command
uint32_t g_lastTickMs = 0;

} // namespace

void servosInit() {
    // Allocate LEDC timers 0 to 2 only. Timer 3 is reserved for the camera
    // XCLK (see main.cpp); letting the servos reconfigure it to 50 Hz stops
    // the camera and every capture fails afterwards.
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);

    g_panServo.setPeriodHertz(50);
    g_tiltServo.setPeriodHertz(50);
    g_doorServo.setPeriodHertz(50);

    g_panServo.attach(kPanServoPin);
    g_tiltServo.attach(kTiltServoPin);
    g_doorServo.attach(kDoorServoPin);

    servoMotionInit(g_pan, kServoCenterAngle);
    servoMotionInit(g_tilt, kServoCenterAngle);
    doorMotionInit(g_door);  // deliberately no g_doorServo.write() at boot
    g_panServo.write(kServoCenterAngle);
    g_tiltServo.write(kServoCenterAngle);
    g_lastTickMs = millis();
}

void applyPanTilt(float panDelta, float tiltDelta) {
    // Targets only; servoTick() performs the motion.
    servoMotionCommand(g_pan, panDelta);
    servoMotionCommand(g_tilt, tiltDelta);
}

void servoTick() {
    const uint32_t steps = servoClockSteps(g_lastTickMs, millis());
    if (steps == 0) return;

    for (uint32_t i = 0; i < steps; i++) {
        servoMotionStep(g_pan);
        servoMotionStep(g_tilt);
        doorMotionStep(g_door);
    }

    int deg = 0;
    if (servoMotionPendingWrite(g_pan, deg)) g_panServo.write(deg);
    if (servoMotionPendingWrite(g_tilt, deg)) g_tiltServo.write(deg);
    if (doorMotionPendingWrite(g_door, deg)) g_doorServo.write(deg);
}

void doorSetState(DoorState state) {
    doorMotionCommand(g_door, state);
}

void triggerDoorLock() {
    doorSetState(DoorState::Close);
}

#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>

#include <unity.h>

#include "frame_response.h"
#include "servo_motion.h"

// Measured full swing 0 -> 180 is 114 ticks (2.28 s); asserted with 20 percent margin.
static const int kMeasuredSwingTicks = 114;
static const int kSwingTickBudget = (kMeasuredSwingTicks * 120) / 100;

void setUp(void) {}
void tearDown(void) {}

static void load(JsonDocument &doc, const char *json) {
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE_MESSAGE(err, "test JSON must parse");
}

struct DoorRun {
    int writes;
    int lastDeg;
    int maxJump;       // largest difference between consecutive written degrees
    float peakSpeed;
    float minTheta;
    float maxTheta;
    float overshoot;   // peak distance past the target
    int ticks;         // ticks until theta == target and v == 0
};

// Steps up to `maxTicks`, collecting writes. Stops early once settled.
static DoorRun runDoor(DoorMotion &d, int maxTicks, int startDeg) {
    DoorRun r = {0, startDeg, 0, 0.0f, d.motion.theta, d.motion.theta, 0.0f, maxTicks};
    const float target = d.motion.target;
    const float dir = (target >= d.motion.theta) ? 1.0f : -1.0f;
    for (int i = 0; i < maxTicks; i++) {
        doorMotionStep(d);
        if (fabsf(d.motion.v) > r.peakSpeed) r.peakSpeed = fabsf(d.motion.v);
        if (d.motion.theta < r.minTheta) r.minTheta = d.motion.theta;
        if (d.motion.theta > r.maxTheta) r.maxTheta = d.motion.theta;
        const float past = (d.motion.theta - target) * dir;
        if (past > r.overshoot) r.overshoot = past;
        int deg = 0;
        if (doorMotionPendingWrite(d, deg)) {
            const int jump = deg > r.lastDeg ? deg - r.lastDeg : r.lastDeg - deg;
            if (jump > r.maxJump) r.maxJump = jump;
            r.lastDeg = deg;
            r.writes++;
        }
        if (d.motion.theta == target && d.motion.v == 0.0f) {
            r.ticks = i + 1;
            break;
        }
    }
    return r;
}

// Applies a command then drains the first pending write (the start write).
static int firstWrite(DoorMotion &d, DoorState s) {
    doorMotionCommand(d, s);
    int deg = -99;
    TEST_ASSERT_TRUE(doorMotionPendingWrite(d, deg));
    return deg;
}

// ---- Tracer ----------------------------------------------------------------

void test_tracer_fresh_door_has_no_pending_write(void) {
    DoorMotion d;
    doorMotionInit(d);
    int deg = -99;
    TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
    for (int i = 0; i < 50; i++) doorMotionStep(d);
    TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
    TEST_ASSERT_FALSE(d.started);
}

void test_tracer_parse_door_open(void) {
    JsonDocument doc;
    load(doc, "{\"m\":{\"actuator\":\"door\",\"action\":{\"state\":\"open\"}}}");
    ManualCommand mc = parseManualCommand(doc["m"]);
    TEST_ASSERT_TRUE(mc.present);
    TEST_ASSERT_TRUE(mc.actuator == ActuatorKind::Door);
    TEST_ASSERT_TRUE(mc.doorState == DoorState::Open);
}

void test_tracer_first_open_writes_zero_once(void) {
    JsonDocument doc;
    load(doc, "{\"m\":{\"actuator\":\"door\",\"action\":{\"state\":\"open\"}}}");
    ManualCommand mc = parseManualCommand(doc["m"]);

    DoorMotion d;
    doorMotionInit(d);
    doorMotionCommand(d, mc.doorState);
    int deg = -99;
    TEST_ASSERT_TRUE(doorMotionPendingWrite(d, deg));
    TEST_ASSERT_EQUAL_INT(0, deg);
    for (int i = 0; i < 100; i++) {
        doorMotionStep(d);
        TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.motion.theta);
}

void test_tracer_close_after_open_ramps_to_180(void) {
    DoorMotion d;
    doorMotionInit(d);
    TEST_ASSERT_EQUAL_INT(0, firstWrite(d, DoorState::Open));
    doorMotionCommand(d, DoorState::Close);

    int prev = 0;
    int writes = 0;
    float peak = 0.0f;
    for (int i = 0; i < 1000; i++) {
        doorMotionStep(d);
        if (fabsf(d.motion.v) > peak) peak = fabsf(d.motion.v);
        TEST_ASSERT_TRUE(d.motion.theta >= 0.0f && d.motion.theta <= 180.0f);
        int deg = 0;
        if (doorMotionPendingWrite(d, deg)) {
            TEST_ASSERT_TRUE(deg > prev);  // monotonic increase
            prev = deg;
            writes++;
        }
    }
    TEST_ASSERT_TRUE(writes > 20);  // a ramp, not a snap
    TEST_ASSERT_TRUE(peak <= kServoMaxVelDegPerSec + 1e-3f);
    TEST_ASSERT_EQUAL_INT(180, prev);
    TEST_ASSERT_EQUAL_FLOAT(180.0f, d.motion.theta);
}

// ---- Expansion -------------------------------------------------------------

void test_first_close_writes_180_once(void) {
    DoorMotion d;
    doorMotionInit(d);
    TEST_ASSERT_EQUAL_INT(180, firstWrite(d, DoorState::Close));
    int deg = -99;
    for (int i = 0; i < 100; i++) {
        doorMotionStep(d);
        TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
    }
}

void test_open_close_open_reaches_each_angle(void) {
    DoorMotion d;
    doorMotionInit(d);
    TEST_ASSERT_EQUAL_INT(0, firstWrite(d, DoorState::Open));

    doorMotionCommand(d, DoorState::Close);
    DoorRun a = runDoor(d, kSwingTickBudget, 0);
    TEST_ASSERT_EQUAL_FLOAT(180.0f, d.motion.theta);
    TEST_ASSERT_EQUAL_INT(180, a.lastDeg);
    TEST_ASSERT_TRUE(a.overshoot <= 0.5f);
    TEST_ASSERT_TRUE(a.ticks <= kSwingTickBudget);
    TEST_ASSERT_TRUE(a.minTheta >= 0.0f && a.maxTheta <= 180.0f);
    TEST_ASSERT_TRUE(a.peakSpeed <= kServoMaxVelDegPerSec + 1e-3f);

    doorMotionCommand(d, DoorState::Open);
    DoorRun b = runDoor(d, kSwingTickBudget, 180);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.motion.theta);
    TEST_ASSERT_EQUAL_INT(0, b.lastDeg);
    TEST_ASSERT_TRUE(b.overshoot <= 0.5f);
    TEST_ASSERT_TRUE(b.ticks <= kSwingTickBudget);
}

void test_retarget_mid_swing_has_no_jump(void) {
    DoorMotion d;
    doorMotionInit(d);
    TEST_ASSERT_EQUAL_INT(0, firstWrite(d, DoorState::Open));
    doorMotionCommand(d, DoorState::Close);

    int last = 0;
    int maxJump = 0;
    for (int i = 0; i < 40; i++) {  // partway through the opening swing
        doorMotionStep(d);
        int deg = 0;
        if (doorMotionPendingWrite(d, deg)) {
            const int j = deg - last;
            if ((j < 0 ? -j : j) > maxJump) maxJump = (j < 0 ? -j : j);
            last = deg;
        }
    }
    TEST_ASSERT_TRUE(last > 0 && last < 180);
    doorMotionCommand(d, DoorState::Open);  // redirect while closing
    for (int i = 0; i < 400; i++) {
        doorMotionStep(d);
        int deg = 0;
        if (doorMotionPendingWrite(d, deg)) {
            const int j = deg - last;
            if ((j < 0 ? -j : j) > maxJump) maxJump = (j < 0 ? -j : j);
            last = deg;
        }
    }
    TEST_ASSERT_TRUE(maxJump <= 2);
    TEST_ASSERT_EQUAL_INT(0, last);
}

void test_repeat_command_is_idempotent(void) {
    DoorMotion d;
    doorMotionInit(d);
    TEST_ASSERT_EQUAL_INT(180, firstWrite(d, DoorState::Close));
    int deg = -99;
    for (int n = 0; n < 5; n++) {
        doorMotionCommand(d, DoorState::Close);
        for (int i = 0; i < 20; i++) {
            doorMotionStep(d);
            TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
        }
    }
    // Same at the open end after a full swing.
    doorMotionCommand(d, DoorState::Open);
    runDoor(d, kSwingTickBudget, 180);
    for (int n = 0; n < 5; n++) {
        doorMotionCommand(d, DoorState::Open);
        for (int i = 0; i < 20; i++) {
            doorMotionStep(d);
            TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
        }
    }
}

void test_unknown_state_changes_nothing(void) {
    DoorMotion d;
    doorMotionInit(d);
    doorMotionCommand(d, DoorState::Unknown);
    TEST_ASSERT_FALSE(d.started);
    int deg = -99;
    doorMotionStep(d);
    TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));

    TEST_ASSERT_EQUAL_INT(0, firstWrite(d, DoorState::Open));
    doorMotionCommand(d, DoorState::Unknown);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.motion.target);
    for (int i = 0; i < 20; i++) {
        doorMotionStep(d);
        TEST_ASSERT_FALSE(doorMotionPendingWrite(d, deg));
    }
}

void test_angle_for_states(void) {
    TEST_ASSERT_EQUAL_INT(0, doorAngleFor(DoorState::Open));
    TEST_ASSERT_EQUAL_INT(180, doorAngleFor(DoorState::Close));
    TEST_ASSERT_TRUE(doorAngleFor(DoorState::Unknown) < 0);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_tracer_fresh_door_has_no_pending_write);
    RUN_TEST(test_tracer_parse_door_open);
    RUN_TEST(test_tracer_first_open_writes_zero_once);
    RUN_TEST(test_tracer_close_after_open_ramps_to_180);
    RUN_TEST(test_first_close_writes_180_once);
    RUN_TEST(test_open_close_open_reaches_each_angle);
    RUN_TEST(test_retarget_mid_swing_has_no_jump);
    RUN_TEST(test_repeat_command_is_idempotent);
    RUN_TEST(test_unknown_state_changes_nothing);
    RUN_TEST(test_angle_for_states);
    return UNITY_END();
}

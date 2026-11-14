#include <math.h>
#include <stdint.h>

#include <unity.h>

#include "servo_motion.h"

static const float kEps = 1e-3f;
static const float kMaxOvershootDeg = 0.5f;
static const int kTickBudget = 2000;

void setUp(void) {}
void tearDown(void) {}

// ---- Helpers ---------------------------------------------------------------

struct RunResult {
    float overshoot;   // peak distance past the target, on the float theta
    float peakSpeed;
    int ticks;         // ticks until theta == target and v == 0
    float minTheta;
    float maxTheta;
    int writes;        // true results from servoMotionPendingWrite
    bool monotonic;
    bool distinct;     // every reported degree differs from the previous one
};

// Starts at `from`, aims at `to`, steps until settled or the tick budget ends.
static RunResult runStep(float from, float to) {
    ServoMotion m;
    servoMotionInit(m, from);
    m.target = to;

    RunResult r = {0.0f, 0.0f, 0, from, from, 0, true, true};
    const float dir = (to >= from) ? 1.0f : -1.0f;
    int prevDeg = m.written;
    int lastDelta = 0;

    for (int i = 0; i < kTickBudget; i++) {
        servoMotionStep(m);
        r.ticks = i + 1;
        const float past = (m.theta - to) * dir;
        if (past > r.overshoot) r.overshoot = past;
        if (fabsf(m.v) > r.peakSpeed) r.peakSpeed = fabsf(m.v);
        if (m.theta < r.minTheta) r.minTheta = m.theta;
        if (m.theta > r.maxTheta) r.maxTheta = m.theta;

        int deg = 0;
        if (servoMotionPendingWrite(m, deg)) {
            r.writes++;
            if (deg == prevDeg) r.distinct = false;
            const int d = deg - prevDeg;
            if (lastDelta != 0 && ((d > 0) != (lastDelta > 0))) r.monotonic = false;
            lastDelta = d;
            prevDeg = deg;
        }
        if (m.theta == to && m.v == 0.0f) break;
    }
    return r;
}

// Drives a 10 deg motion from a simulated millis() clock that starts at
// startMs. Returns the number of writes through `writes`, final angle returned.
static float driveClock(uint32_t startMs, int *writes) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    servoMotionCommand(m, 1.0f);

    uint32_t last = startMs;
    *writes = 0;
    for (uint32_t t = 1; t <= 2000u; t++) {
        const uint32_t now = (uint32_t)(startMs + t);
        const uint32_t steps = servoClockSteps(last, now);
        for (uint32_t i = 0; i < steps; i++) servoMotionStep(m);
        int deg = 0;
        if (steps > 0 && servoMotionPendingWrite(m, deg)) (*writes)++;
    }
    return m.theta;
}

// ---- Tracer behaviors ------------------------------------------------------

void test_tracer_pan_command_reaches_target(void) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    servoMotionCommand(m, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 100.0f, m.target);
    for (int i = 0; i < 300; i++) servoMotionStep(m);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, m.theta);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.v);
}

void test_tracer_pending_write_only_on_degree_change(void) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    int deg = -1;
    TEST_ASSERT_FALSE(servoMotionPendingWrite(m, deg));
    m.theta = 90.4f;
    TEST_ASSERT_FALSE(servoMotionPendingWrite(m, deg));
    m.theta = 90.6f;
    TEST_ASSERT_TRUE(servoMotionPendingWrite(m, deg));
    TEST_ASSERT_EQUAL_INT(91, deg);
    TEST_ASSERT_FALSE(servoMotionPendingWrite(m, deg));
}

void test_tracer_clock_steps_threshold(void) {
    uint32_t last = 1000;
    TEST_ASSERT_EQUAL_UINT32(0, servoClockSteps(last, 1019));
    TEST_ASSERT_EQUAL_UINT32(1000, last);
    TEST_ASSERT_EQUAL_UINT32(1, servoClockSteps(last, 1020));
    TEST_ASSERT_EQUAL_UINT32(1020, last);
}

// ---- A: constants ----------------------------------------------------------

void test_constants(void) {
    TEST_ASSERT_EQUAL_FLOAT(10.0f, kServoMaxStepDegrees);
    TEST_ASSERT_EQUAL_UINT32(20, kServoTickMs);
    TEST_ASSERT_EQUAL_FLOAT(0.02f, kServoTickDt);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, kServoGainPerSec);
    TEST_ASSERT_EQUAL_FLOAT(400.0f, kServoMaxAccelDegPerSec2);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, kServoMaxVelDegPerSec);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, kServoSettleDistDeg);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, kServoSettleVelDegPerSec);
    TEST_ASSERT_EQUAL_UINT32(5, kServoMaxCatchUpTicks);
}

// ---- B: command ------------------------------------------------------------

void test_command_adds_clamps_and_ignores_non_finite(void) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    servoMotionCommand(m, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, m.target);
    servoMotionCommand(m, -1.0f);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, m.target);

    for (int i = 0; i < 30; i++) servoMotionCommand(m, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(180.0f, m.target);
    for (int i = 0; i < 30; i++) servoMotionCommand(m, -1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.target);

    servoMotionInit(m, 90.0f);
    servoMotionCommand(m, NAN);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, m.target);
    servoMotionCommand(m, INFINITY);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, m.target);
    servoMotionCommand(m, -INFINITY);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, m.target);
}

// ---- C: convergence and damping -------------------------------------------

static void assertConverges(float from, float to) {
    RunResult r = runStep(from, to);
    TEST_ASSERT_LESS_THAN_INT(kTickBudget, r.ticks);
    TEST_ASSERT_TRUE(r.overshoot <= kMaxOvershootDeg);
    // Settled within the anti-hunting thresholds (actually exact after snap).
    ServoMotion m;
    servoMotionInit(m, from);
    m.target = to;
    for (int i = 0; i < r.ticks; i++) servoMotionStep(m);
    TEST_ASSERT_TRUE(fabsf(m.target - m.theta) < kServoSettleDistDeg);
    TEST_ASSERT_TRUE(fabsf(m.v) < kServoSettleVelDegPerSec);
}

void test_convergence_and_damping(void) {
    const float steps[] = {10.0f, 40.0f, 180.0f};
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        const float s = steps[i];
        if (s < 180.0f) {
            assertConverges(90.0f, 90.0f + s > 180.0f ? 180.0f : 90.0f + s);
            assertConverges(90.0f, 90.0f - s < 0.0f ? 0.0f : 90.0f - s);
        }
        assertConverges(0.0f, s);
        assertConverges(180.0f, 180.0f - s);
    }
    assertConverges(0.0f, 180.0f);
    assertConverges(180.0f, 0.0f);
}

// ---- D: sweep --------------------------------------------------------------

void test_sweep_every_step_1_to_180_overshoot(void) {
    for (int s = 1; s <= 180; s++) {
        RunResult up = runStep(0.0f, (float)s);
        RunResult down = runStep(180.0f, 180.0f - (float)s);
        TEST_ASSERT_TRUE_MESSAGE(up.overshoot <= kMaxOvershootDeg, "up overshoot");
        TEST_ASSERT_TRUE_MESSAGE(down.overshoot <= kMaxOvershootDeg, "down overshoot");
        TEST_ASSERT_LESS_THAN_INT(kTickBudget, up.ticks);
        TEST_ASSERT_LESS_THAN_INT(kTickBudget, down.ticks);
    }
}

// ---- E: limits and speed ---------------------------------------------------

void test_limits_and_speed_cap(void) {
    for (int s = 1; s <= 180; s++) {
        RunResult up = runStep(0.0f, (float)s);
        RunResult down = runStep(180.0f, 180.0f - (float)s);
        TEST_ASSERT_TRUE(up.peakSpeed <= kServoMaxVelDegPerSec + kEps);
        TEST_ASSERT_TRUE(down.peakSpeed <= kServoMaxVelDegPerSec + kEps);
        TEST_ASSERT_TRUE(up.minTheta >= 0.0f && up.maxTheta <= 180.0f);
        TEST_ASSERT_TRUE(down.minTheta >= 0.0f && down.maxTheta <= 180.0f);
    }

    // Drive commands past both edges while stepping.
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    for (int i = 0; i < 400; i++) {
        servoMotionCommand(m, (i / 100) % 2 == 0 ? 1.0f : -1.0f);
        servoMotionStep(m);
        TEST_ASSERT_TRUE(m.theta >= 0.0f && m.theta <= 180.0f);
        TEST_ASSERT_TRUE(m.target >= 0.0f && m.target <= 180.0f);
        TEST_ASSERT_TRUE(fabsf(m.v) <= kServoMaxVelDegPerSec + kEps);
    }
}

// ---- F: settle time --------------------------------------------------------

void test_ten_degree_step_settles_within_30_ticks(void) {
    RunResult up = runStep(90.0f, 100.0f);
    RunResult down = runStep(90.0f, 80.0f);
    TEST_ASSERT_TRUE(up.ticks <= 30);
    TEST_ASSERT_TRUE(down.ticks <= 30);
}

// ---- G: idle ---------------------------------------------------------------

void test_idle_target_equals_theta_does_nothing(void) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    const float theta0 = m.theta;
    for (int i = 0; i < 100; i++) {
        servoMotionStep(m);
        int deg = 0;
        TEST_ASSERT_FALSE(servoMotionPendingWrite(m, deg));
    }
    TEST_ASSERT_EQUAL_FLOAT(theta0, m.theta);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.v);
    TEST_ASSERT_EQUAL_FLOAT(m.target, m.theta);
}

// ---- H: write on change ----------------------------------------------------

void test_write_on_change_during_40_degree_step(void) {
    RunResult r = runStep(90.0f, 130.0f);
    TEST_ASSERT_TRUE(r.writes <= 41);
    TEST_ASSERT_TRUE(r.writes >= 1);
    TEST_ASSERT_TRUE(r.distinct);
    TEST_ASSERT_TRUE(r.monotonic);
}

// ---- I: retarget -----------------------------------------------------------

void test_retarget_mid_motion_latest_wins(void) {
    ServoMotion m;
    servoMotionInit(m, 90.0f);
    float overshoot = 0.0f;

    servoMotionCommand(m, 1.0f);
    for (int i = 0; i < 5; i++) servoMotionStep(m);
    servoMotionCommand(m, -1.0f);
    for (int i = 0; i < 5; i++) servoMotionStep(m);
    servoMotionCommand(m, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, m.target);

    for (int i = 0; i < 300; i++) {
        servoMotionStep(m);
        TEST_ASSERT_TRUE(m.theta >= 0.0f && m.theta <= 180.0f);
        if (m.theta - 100.0f > overshoot) overshoot = m.theta - 100.0f;
    }
    TEST_ASSERT_EQUAL_FLOAT(100.0f, m.theta);
    TEST_ASSERT_TRUE(overshoot <= kMaxOvershootDeg);
}

// ---- J: clock --------------------------------------------------------------

void test_clock_steps_cap_and_resync(void) {
    uint32_t last = 5000;
    TEST_ASSERT_EQUAL_UINT32(0, servoClockSteps(last, 5019));
    TEST_ASSERT_EQUAL_UINT32(1, servoClockSteps(last, 5020 + 0));
    TEST_ASSERT_EQUAL_UINT32(5020, last);

    last = 1000;
    TEST_ASSERT_EQUAL_UINT32(3, servoClockSteps(last, 1060));
    TEST_ASSERT_EQUAL_UINT32(1060, last);

    last = 1000;
    TEST_ASSERT_EQUAL_UINT32(kServoMaxCatchUpTicks, servoClockSteps(last, 3000));
    TEST_ASSERT_EQUAL_UINT32(3000, last);
}

// ---- K: wrap ---------------------------------------------------------------

void test_clock_wrap_around(void) {
    uint32_t last = 0xFFFFFFF0u;
    const uint32_t now = 10u;  // elapsed 26 ms across the rollover
    TEST_ASSERT_EQUAL_UINT32(1, servoClockSteps(last, now));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF0u + 20u, last);

    int writesNoWrap = 0;
    int writesWrap = 0;
    const float noWrap = driveClock(1000u, &writesNoWrap);
    const float wrap = driveClock(0xFFFFFF00u, &writesWrap);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, noWrap);
    TEST_ASSERT_EQUAL_FLOAT(noWrap, wrap);
    TEST_ASSERT_EQUAL_INT(writesNoWrap, writesWrap);
    TEST_ASSERT_TRUE(writesNoWrap > 0);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_tracer_pan_command_reaches_target);
    RUN_TEST(test_tracer_pending_write_only_on_degree_change);
    RUN_TEST(test_tracer_clock_steps_threshold);
    RUN_TEST(test_constants);
    RUN_TEST(test_command_adds_clamps_and_ignores_non_finite);
    RUN_TEST(test_convergence_and_damping);
    RUN_TEST(test_sweep_every_step_1_to_180_overshoot);
    RUN_TEST(test_limits_and_speed_cap);
    RUN_TEST(test_ten_degree_step_settles_within_30_ticks);
    RUN_TEST(test_idle_target_equals_theta_does_nothing);
    RUN_TEST(test_write_on_change_during_40_degree_step);
    RUN_TEST(test_retarget_mid_motion_latest_wins);
    RUN_TEST(test_clock_steps_cap_and_resync);
    RUN_TEST(test_clock_wrap_around);
    return UNITY_END();
}

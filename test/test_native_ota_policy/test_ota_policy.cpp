#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "ota_policy.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Constants ---------------------------------------------------------------

void test_constants(void) {
    TEST_ASSERT_EQUAL_STRING("securiot-cam", OTA_HOSTNAME);
    for (const char *p = OTA_HOSTNAME; *p; p++) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-';
        TEST_ASSERT_TRUE(ok);
    }
    TEST_ASSERT_EQUAL_INT(3232, OTA_PORT);
    TEST_ASSERT_EQUAL_INT(10, OTA_PROGRESS_STEP_PERCENT);
}

// ---- Password ------------------------------------------------------------------

void test_password_usable(void) {
    TEST_ASSERT_FALSE(otaPasswordUsable(NULL));
    TEST_ASSERT_FALSE(otaPasswordUsable(""));
    TEST_ASSERT_TRUE(otaPasswordUsable("x"));
    TEST_ASSERT_TRUE(otaPasswordUsable("a longer password 123"));
}

// ---- Start and stop decisions --------------------------------------------------

void test_next_action_truth_table(void) {
    for (int mask = 0; mask < 16; mask++) {
        bool usable = (mask & 8) != 0;
        bool running = (mask & 4) != 0;
        bool staUp = (mask & 2) != 0;
        bool portal = (mask & 1) != 0;
        OtaAction a = otaNextAction(usable, running, staUp, portal);
        OtaAction expect = OTA_ACTION_NONE;
        if (usable && !running && staUp && !portal) expect = OTA_ACTION_START;
        if (usable && running && portal) expect = OTA_ACTION_STOP;
        TEST_ASSERT_EQUAL_INT(expect, a);
    }
}

void test_next_action_explicit_cases(void) {
    // Without a usable password nothing ever happens.
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_NONE, otaNextAction(false, false, true, false));
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_NONE, otaNextAction(false, true, true, true));
    // A transient STA loss does not stop a running service.
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_NONE, otaNextAction(true, true, false, false));
    // The portal does.
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_STOP, otaNextAction(true, true, true, true));
    // No start while the portal is begun.
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_NONE, otaNextAction(true, false, true, true));
    TEST_ASSERT_EQUAL_INT(OTA_ACTION_START, otaNextAction(true, false, true, false));
}

// ---- Progress tracker ----------------------------------------------------------

void test_progress_first_steps(void) {
    OtaProgress p;
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 0, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 99, 1000));
    TEST_ASSERT_EQUAL_INT(10, otaProgressStep(p, 100, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 101, 1000));
}

void test_progress_jump_reports_once(void) {
    OtaProgress p;
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(10, otaProgressStep(p, 100, 1000));
    TEST_ASSERT_EQUAL_INT(40, otaProgressStep(p, 400, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 400, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 410, 1000));
}

void test_progress_completion(void) {
    OtaProgress p;
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(100, otaProgressStep(p, 1000, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 1000, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 1000, 1000));
    OtaProgress q;
    otaProgressReset(q);
    TEST_ASSERT_EQUAL_INT(100, otaProgressStep(q, 1500, 1000));
}

void test_progress_edge_inputs(void) {
    OtaProgress p;
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 500, 0));
    TEST_ASSERT_EQUAL_INT(90, otaProgressStep(p, 4000000000u, 4294967295u));
}

void test_progress_never_repeats_a_smaller_step(void) {
    OtaProgress p;
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(50, otaProgressStep(p, 500, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 300, 1000));
    TEST_ASSERT_EQUAL_INT(-1, otaProgressStep(p, 500, 1000));
    TEST_ASSERT_EQUAL_INT(60, otaProgressStep(p, 600, 1000));
    otaProgressReset(p);
    TEST_ASSERT_EQUAL_INT(10, otaProgressStep(p, 100, 1000));
}

// ---- Error names ---------------------------------------------------------------

void test_error_names(void) {
    TEST_ASSERT_EQUAL_STRING("auth", otaErrorName(0));
    TEST_ASSERT_EQUAL_STRING("begin", otaErrorName(1));
    TEST_ASSERT_EQUAL_STRING("connect", otaErrorName(2));
    TEST_ASSERT_EQUAL_STRING("receive", otaErrorName(3));
    TEST_ASSERT_EQUAL_STRING("end", otaErrorName(4));
    TEST_ASSERT_EQUAL_STRING("unknown", otaErrorName(5));
    TEST_ASSERT_EQUAL_STRING("unknown", otaErrorName(-1));
}

// ---- Formatters ----------------------------------------------------------------

void test_progress_log_exact(void) {
    char out[64];
    otaFormatProgressLog(out, sizeof(out), 40);
    TEST_ASSERT_EQUAL_STRING("OTA: progress 40%", out);
}

void test_error_log_exact(void) {
    char out[64];
    otaFormatErrorLog(out, sizeof(out), 0);
    TEST_ASSERT_EQUAL_STRING("OTA: error code=0 (auth)", out);
    otaFormatErrorLog(out, sizeof(out), 4);
    TEST_ASSERT_EQUAL_STRING("OTA: error code=4 (end)", out);
    otaFormatErrorLog(out, sizeof(out), 9);
    TEST_ASSERT_EQUAL_STRING("OTA: error code=9 (unknown)", out);
}

void test_ready_log_exact(void) {
    char out[96];
    otaFormatReadyLog(out, sizeof(out), 192, 168, 1, 37);
    TEST_ASSERT_EQUAL_STRING("OTA: ready host=securiot-cam.local ip=192.168.1.37 port=3232", out);
}

void test_formatters_buffer_contract(void) {
    char buf[17];
    char guard[4];

    memset(buf, 'x', sizeof(buf));
    otaFormatProgressLog(buf, 16, 40);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);

    memset(buf, 'x', sizeof(buf));
    otaFormatErrorLog(buf, 16, 4);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);

    memset(buf, 'x', sizeof(buf));
    otaFormatReadyLog(buf, 16, 192, 168, 1, 37);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);

    memset(guard, 'x', sizeof(guard));
    TEST_ASSERT_EQUAL_INT(0, otaFormatProgressLog(NULL, 16, 40));
    TEST_ASSERT_EQUAL_INT(0, otaFormatProgressLog(guard, 0, 40));
    TEST_ASSERT_EQUAL_INT(0, otaFormatErrorLog(NULL, 16, 4));
    TEST_ASSERT_EQUAL_INT(0, otaFormatErrorLog(guard, 0, 4));
    TEST_ASSERT_EQUAL_INT(0, otaFormatReadyLog(NULL, 16, 1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT(0, otaFormatReadyLog(guard, 0, 1, 2, 3, 4));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)guard[0]);
}

// ---- Secret-free ---------------------------------------------------------------

static bool containsNoCase(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        for (; i < n; i++) {
            char a = hay[i];
            char b = needle[i];
            if (a == '\0') return false;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == n) return true;
    }
    return false;
}

void test_formatted_lines_are_secret_free(void) {
    char out[96];
    const char *lines[3];
    char a[96], b[96], c[96];
    otaFormatProgressLog(a, sizeof(a), 40);
    otaFormatErrorLog(b, sizeof(b), 0);
    otaFormatReadyLog(c, sizeof(c), 192, 168, 1, 37);
    lines[0] = a;
    lines[1] = b;
    lines[2] = c;
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_FALSE(containsNoCase(lines[i], "pass"));
        TEST_ASSERT_NULL(strstr(lines[i], "://"));
        TEST_ASSERT_NULL(strchr(lines[i], '?'));
    }
    (void)out;
}

void test_fixed_texts_exact(void) {
    TEST_ASSERT_EQUAL_STRING("OTA: update started", OTA_LOG_STARTED);
    TEST_ASSERT_EQUAL_STRING("OTA: update finished, restarting", OTA_LOG_FINISHED);
    TEST_ASSERT_EQUAL_STRING("OTA: stopped, the setup portal is open", OTA_LOG_STOPPED);
    TEST_ASSERT_EQUAL_STRING("OTA: disabled, OTA_PASSWORD is not set in secrets.h", OTA_LOG_DISABLED);
    const char *texts[] = {OTA_LOG_STARTED, OTA_LOG_FINISHED, OTA_LOG_STOPPED, OTA_LOG_DISABLED};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        TEST_ASSERT_NULL(strstr(texts[i], "://"));
        TEST_ASSERT_NULL(strchr(texts[i], '?'));
    }
    // Only the disabled line may name the macro.
    TEST_ASSERT_FALSE(containsNoCase(OTA_LOG_STARTED, "pass"));
    TEST_ASSERT_FALSE(containsNoCase(OTA_LOG_FINISHED, "pass"));
    TEST_ASSERT_FALSE(containsNoCase(OTA_LOG_STOPPED, "pass"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_constants);
    RUN_TEST(test_password_usable);
    RUN_TEST(test_next_action_truth_table);
    RUN_TEST(test_next_action_explicit_cases);
    RUN_TEST(test_progress_first_steps);
    RUN_TEST(test_progress_jump_reports_once);
    RUN_TEST(test_progress_completion);
    RUN_TEST(test_progress_edge_inputs);
    RUN_TEST(test_progress_never_repeats_a_smaller_step);
    RUN_TEST(test_error_names);
    RUN_TEST(test_progress_log_exact);
    RUN_TEST(test_error_log_exact);
    RUN_TEST(test_ready_log_exact);
    RUN_TEST(test_formatters_buffer_contract);
    RUN_TEST(test_formatted_lines_are_secret_free);
    RUN_TEST(test_fixed_texts_exact);
    return UNITY_END();
}

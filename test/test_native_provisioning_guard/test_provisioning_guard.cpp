#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "provisioning_guard.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Test 1: watchdog budget ---------------------------------------------

void test_ping_budget_fits_under_task_watchdog(void) {
    TEST_ASSERT_EQUAL_INT(1500, PROVISIONING_PING_CONNECT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(1500, PROVISIONING_PING_RESPONSE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(3000, PROVISIONING_PING_BUDGET_MS);
    TEST_ASSERT_EQUAL_INT(5000, PROVISIONING_TASK_WDT_MS);
    TEST_ASSERT_TRUE(PROVISIONING_PING_CONNECT_TIMEOUT_MS + PROVISIONING_PING_RESPONSE_TIMEOUT_MS <= PROVISIONING_PING_BUDGET_MS);
    TEST_ASSERT_TRUE(PROVISIONING_PING_BUDGET_MS + 1500 <= PROVISIONING_TASK_WDT_MS);
}

// ---- Test 2: reachability --------------------------------------------------

void test_ping_reachable_true_for_any_http_status(void) {
    TEST_ASSERT_TRUE(provisioningPingReachable(200));
    TEST_ASSERT_TRUE(provisioningPingReachable(301));
    TEST_ASSERT_TRUE(provisioningPingReachable(404));
    TEST_ASSERT_TRUE(provisioningPingReachable(500));
}

void test_ping_reachable_false_for_zero_and_errors(void) {
    TEST_ASSERT_FALSE(provisioningPingReachable(0));
    TEST_ASSERT_FALSE(provisioningPingReachable(-1));
    TEST_ASSERT_FALSE(provisioningPingReachable(-4));
    TEST_ASSERT_FALSE(provisioningPingReachable(-11));
}

// ---- Test 3: wrap-safe elapsed time ---------------------------------------

void test_elapsed_basic_wrap_and_equal(void) {
    TEST_ASSERT_EQUAL_UINT32(37, provisioningElapsedMs(1000, 1037));
    TEST_ASSERT_EQUAL_UINT32(0x200, provisioningElapsedMs(0xFFFFFF00u, 0x00000100u));
    TEST_ASSERT_EQUAL_UINT32(0, provisioningElapsedMs(5000, 5000));
}

// ---- Test 4: ping log line -------------------------------------------------

void test_ping_log_exact_lines(void) {
    char out[64];
    provisioningFormatPingLog(out, sizeof(out), 200, 37);
    TEST_ASSERT_EQUAL_STRING("Provisioning: Edge API ping code=200 elapsed_ms=37", out);
    provisioningFormatPingLog(out, sizeof(out), -1, 1502);
    TEST_ASSERT_EQUAL_STRING("Provisioning: Edge API ping code=-1 elapsed_ms=1502", out);
}

void test_ping_log_truncates_with_guard(void) {
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    provisioningFormatPingLog(buf, 16, 200, 37);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT32(15, strlen(buf));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
}

void test_ping_log_null_and_zero_size_are_noop(void) {
    char out[8];
    memset(out, 'x', sizeof(out));
    provisioningFormatPingLog(NULL, 8, 200, 37);
    provisioningFormatPingLog(out, 0, 200, 37);
    for (size_t i = 0; i < sizeof(out); ++i) {
        TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)out[i]);
    }
}

void test_ping_log_has_no_url_material(void) {
    char out[64];
    const int codes[] = {200, -1, -11, 0, 404};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        provisioningFormatPingLog(out, sizeof(out), codes[i], 1234);
        TEST_ASSERT_NULL(strstr(out, "http"));
        TEST_ASSERT_NULL(strstr(out, "://"));
        TEST_ASSERT_NULL(strchr(out, '?'));
    }
}

// ---- Test 5: boot decision -------------------------------------------------

void test_boot_decision_matrix(void) {
    TEST_ASSERT_EQUAL_INT(PROVISIONING_BOOT_SKIP_PORTAL, provisioningDecideBoot(true, false));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_BOOT_START_PORTAL, provisioningDecideBoot(true, true));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_BOOT_CLEAR_STALE_AND_START_PORTAL, provisioningDecideBoot(false, false));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_BOOT_CLEAR_STALE_AND_START_PORTAL, provisioningDecideBoot(false, true));
}

// ---- Test 6: PORTAL_COMPLETE verdict ----------------------------------------

void test_portal_complete_classifier(void) {
    TEST_ASSERT_EQUAL_INT(PROVISIONING_PORTAL_ACCEPT, provisioningClassifyPortalComplete(false, 8));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_PORTAL_ACCEPT, provisioningClassifyPortalComplete(false, 1));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_PORTAL_REJECT_AP_MODE, provisioningClassifyPortalComplete(true, 8));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_PORTAL_REJECT_AP_MODE, provisioningClassifyPortalComplete(true, 0));
    TEST_ASSERT_EQUAL_INT(PROVISIONING_PORTAL_REJECT_NO_SSID, provisioningClassifyPortalComplete(false, 0));
}

// ---- Test 7: verdict names ---------------------------------------------------

void test_portal_verdict_names(void) {
    TEST_ASSERT_EQUAL_STRING("accept", provisioningPortalVerdictName(PROVISIONING_PORTAL_ACCEPT));
    TEST_ASSERT_EQUAL_STRING("ap_mode", provisioningPortalVerdictName(PROVISIONING_PORTAL_REJECT_AP_MODE));
    TEST_ASSERT_EQUAL_STRING("no_ssid", provisioningPortalVerdictName(PROVISIONING_PORTAL_REJECT_NO_SSID));
    TEST_ASSERT_EQUAL_STRING("unknown", provisioningPortalVerdictName((ProvisioningPortalVerdict)99));
}

// ---- Test 8: reject log line -------------------------------------------------

void test_portal_reject_log_exact_lines(void) {
    char out[96];
    provisioningFormatPortalRejectLog(out, sizeof(out), PROVISIONING_PORTAL_REJECT_AP_MODE);
    TEST_ASSERT_EQUAL_STRING("Provisioning: PORTAL_COMPLETE rejected reason=ap_mode, restarting portal", out);
    provisioningFormatPortalRejectLog(out, sizeof(out), PROVISIONING_PORTAL_REJECT_NO_SSID);
    TEST_ASSERT_EQUAL_STRING("Provisioning: PORTAL_COMPLETE rejected reason=no_ssid, restarting portal", out);
}

void test_portal_reject_log_accept_is_empty(void) {
    char out[96];
    memset(out, 'x', sizeof(out));
    int n = provisioningFormatPortalRejectLog(out, sizeof(out), PROVISIONING_PORTAL_ACCEPT);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_portal_reject_log_truncates_with_guard(void) {
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    provisioningFormatPortalRejectLog(buf, 16, PROVISIONING_PORTAL_REJECT_AP_MODE);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT32(15, strlen(buf));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
}

void test_portal_reject_log_null_and_zero_size_are_noop(void) {
    char out[8];
    memset(out, 'x', sizeof(out));
    provisioningFormatPortalRejectLog(NULL, 8, PROVISIONING_PORTAL_REJECT_AP_MODE);
    provisioningFormatPortalRejectLog(out, 0, PROVISIONING_PORTAL_REJECT_AP_MODE);
    for (size_t i = 0; i < sizeof(out); ++i) {
        TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)out[i]);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ping_budget_fits_under_task_watchdog);
    RUN_TEST(test_ping_reachable_true_for_any_http_status);
    RUN_TEST(test_ping_reachable_false_for_zero_and_errors);
    RUN_TEST(test_elapsed_basic_wrap_and_equal);
    RUN_TEST(test_ping_log_exact_lines);
    RUN_TEST(test_ping_log_truncates_with_guard);
    RUN_TEST(test_ping_log_null_and_zero_size_are_noop);
    RUN_TEST(test_ping_log_has_no_url_material);
    RUN_TEST(test_boot_decision_matrix);
    RUN_TEST(test_portal_complete_classifier);
    RUN_TEST(test_portal_verdict_names);
    RUN_TEST(test_portal_reject_log_exact_lines);
    RUN_TEST(test_portal_reject_log_accept_is_empty);
    RUN_TEST(test_portal_reject_log_truncates_with_guard);
    RUN_TEST(test_portal_reject_log_null_and_zero_size_are_noop);
    return UNITY_END();
}

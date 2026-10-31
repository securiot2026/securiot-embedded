#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "link_stability.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Power save rising edge ------------------------------------------------

void test_power_save_edge_truth_table(void) {
    TEST_ASSERT_TRUE(linkShouldApplyPowerSave(false, true));
    TEST_ASSERT_FALSE(linkShouldApplyPowerSave(true, true));
    TEST_ASSERT_FALSE(linkShouldApplyPowerSave(false, false));
    TEST_ASSERT_FALSE(linkShouldApplyPowerSave(true, false));
}

// ---- Power save log line ---------------------------------------------------

void test_power_save_log_exact_lines(void) {
    char out[64];
    linkFormatPowerSaveLog(out, sizeof(out), 0);
    TEST_ASSERT_EQUAL_STRING("WiFi: power save disabled", out);
    linkFormatPowerSaveLog(out, sizeof(out), 12289);
    TEST_ASSERT_EQUAL_STRING("WiFi: power save disable failed err=12289", out);
}

void test_power_save_log_truncates_with_guard(void) {
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    linkFormatPowerSaveLog(buf, 16, 0);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT32(15, strlen(buf));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
}

void test_power_save_log_null_and_zero_size_are_noop(void) {
    char buf[4];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, linkFormatPowerSaveLog(NULL, 16, 0));
    TEST_ASSERT_EQUAL_INT(0, linkFormatPowerSaveLog(buf, 0, 0));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[0]);
}

// ---- Frames POST budget ----------------------------------------------------

void test_frames_post_budget_constants(void) {
    TEST_ASSERT_EQUAL_INT(1500, LINK_FRAMES_POST_CONNECT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(2500, LINK_FRAMES_POST_RESPONSE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(4000, LINK_FRAMES_POST_BUDGET_MS);
    TEST_ASSERT_TRUE(LINK_FRAMES_POST_CONNECT_TIMEOUT_MS + LINK_FRAMES_POST_RESPONSE_TIMEOUT_MS <= LINK_FRAMES_POST_BUDGET_MS);
}

// ---- POST failure log line -------------------------------------------------

void test_post_fail_log_exact_lines(void) {
    char out[64];
    linkFormatPostFailLog(out, sizeof(out), -1);
    TEST_ASSERT_EQUAL_STRING("HTTP POST failed: code=-1", out);
    linkFormatPostFailLog(out, sizeof(out), -11);
    TEST_ASSERT_EQUAL_STRING("HTTP POST failed: code=-11", out);
    linkFormatPostFailLog(out, sizeof(out), 0);
    TEST_ASSERT_EQUAL_STRING("HTTP POST failed: code=0", out);
}

void test_post_fail_log_truncates_with_guard(void) {
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    linkFormatPostFailLog(buf, 16, -1);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT32(15, strlen(buf));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
}

void test_post_fail_log_null_and_zero_size_are_noop(void) {
    char buf[4];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, linkFormatPostFailLog(NULL, 16, -1));
    TEST_ASSERT_EQUAL_INT(0, linkFormatPostFailLog(buf, 0, -1));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[0]);
}

void test_post_fail_log_has_no_url_or_misleading_text(void) {
    const int codes[] = {-1, -2, -3, -4, -11, 0};
    char out[64];
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        linkFormatPostFailLog(out, sizeof(out), codes[i]);
        TEST_ASSERT_NULL(strstr(out, "://"));
        TEST_ASSERT_NULL(strchr(out, '?'));
        TEST_ASSERT_NULL(strstr(out, "refused"));
        TEST_ASSERT_NULL(strstr(out, "connection"));
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_power_save_edge_truth_table);
    RUN_TEST(test_power_save_log_exact_lines);
    RUN_TEST(test_power_save_log_truncates_with_guard);
    RUN_TEST(test_power_save_log_null_and_zero_size_are_noop);
    RUN_TEST(test_frames_post_budget_constants);
    RUN_TEST(test_post_fail_log_exact_lines);
    RUN_TEST(test_post_fail_log_truncates_with_guard);
    RUN_TEST(test_post_fail_log_null_and_zero_size_are_noop);
    RUN_TEST(test_post_fail_log_has_no_url_or_misleading_text);
    return UNITY_END();
}

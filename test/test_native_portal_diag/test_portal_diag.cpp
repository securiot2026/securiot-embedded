#include <stdint.h>
#include <string.h>

#include <string>

#include <unity.h>

#include "portal_diag.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Test 1: path only ---------------------------------------------------

void test_sanitize_cuts_query_string(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    size_t n = portalDiagSanitizePath("/provision-extra?edge=http%3A%2F%2Fx&k=abc", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/provision-extra", out);
    TEST_ASSERT_EQUAL_UINT32(strlen("/provision-extra"), n);
}

void test_sanitize_cuts_fragment(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    portalDiagSanitizePath("/a#frag", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/a", out);
}

void test_sanitize_null_and_empty(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    memset(out, 'x', sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(0, portalDiagSanitizePath(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    memset(out, 'x', sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(0, portalDiagSanitizePath("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// ---- Test 2: hostile bytes -----------------------------------------------

void test_sanitize_replaces_hostile_bytes(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    const char hostile[] = "/a\rb\nc\td\x1b" "e\x7f" "f\x80" "g\xff" "h i";
    portalDiagSanitizePath(hostile, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/a.b.c.d.e.f.g.h i", out);
    for (const char *p = out; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        TEST_ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
    }
}

// ---- Test 3: length cap and buffers --------------------------------------

static std::string pathOfLength(size_t total) {
    return std::string("/") + std::string(total - 1, 'a');
}

void test_sanitize_exactly_64_unchanged(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    const std::string in = pathOfLength(64);
    size_t n = portalDiagSanitizePath(in.c_str(), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(in.c_str(), out);
    TEST_ASSERT_EQUAL_UINT32(64, n);
}

void test_sanitize_65_and_200_are_capped(void) {
    char out[PORTAL_DIAG_PATH_BUF_LEN];
    const size_t lengths[] = {65, 200};
    for (size_t i = 0; i < 2; ++i) {
        const std::string in = pathOfLength(lengths[i]);
        size_t n = portalDiagSanitizePath(in.c_str(), out, sizeof(out));
        const std::string expected = in.substr(0, 61) + "...";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), out);
        TEST_ASSERT_EQUAL_UINT32(64, n);
        TEST_ASSERT_EQUAL_UINT32(64, strlen(out));
    }
}

void test_sanitize_tiny_buffer_and_guard(void) {
    char buf[9];
    memset(buf, 'G', sizeof(buf));
    // 8 byte window at buf[0..7], buf[8] is the guard.
    size_t n = portalDiagSanitizePath("/abcdefghij", buf, 8);
    TEST_ASSERT_EQUAL_STRING("/abc...", buf);
    TEST_ASSERT_EQUAL_UINT32(7, n);
    TEST_ASSERT_EQUAL_CHAR('G', buf[8]);
}

void test_sanitize_zero_size_null_out_is_noop(void) {
    TEST_ASSERT_EQUAL_UINT32(0, portalDiagSanitizePath("/abc", NULL, 0));
}

void test_sanitize_size_one_gives_empty(void) {
    char buf[2] = {'G', 'G'};
    size_t n = portalDiagSanitizePath("/abc", buf, 1);
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('G', buf[1]);
}

// ---- Test 4: probe paths -------------------------------------------------

void test_probe_paths_true(void) {
    const char *probes[] = {"/generate_204", "/hotspot-detect.html", "/connecttest.txt",
                            "/ncsi.txt", "/fwlink", "/gen_204", "/library/test/success.html",
                            "/redirect", "/canonical.html", "/success.txt", "/wpad.dat",
                            "/startpage"};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(portalDiagIsProbePath(probes[i]), probes[i]);
    }
}

void test_probe_paths_false(void) {
    const char *others[] = {"/", "/provision-extra", "/espconnect/connect", "/generate_2040",
                            "/FWLINK", ""};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i) {
        TEST_ASSERT_FALSE_MESSAGE(portalDiagIsProbePath(others[i]), others[i]);
    }
    TEST_ASSERT_FALSE(portalDiagIsProbePath(NULL));
}

// ---- Test 5: MAC suffix --------------------------------------------------

void test_mac_suffix_last_three_bytes(void) {
    const uint8_t mac[6] = {0x01, 0x02, 0x03, 0xAB, 0xCD, 0xEF};
    char out[16];
    char *r = portalDiagFormatMacSuffix(mac, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ab:cd:ef", out);
    TEST_ASSERT_EQUAL_PTR(out, r);
    TEST_ASSERT_NULL(strstr(out, "01"));
    TEST_ASSERT_NULL(strstr(out, "02"));
    TEST_ASSERT_NULL(strstr(out, "03"));
}

void test_mac_suffix_null_mac(void) {
    char out[16];
    portalDiagFormatMacSuffix(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("??:??:??", out);
}

void test_mac_suffix_small_buffer(void) {
    const uint8_t mac[6] = {0x01, 0x02, 0x03, 0xAB, 0xCD, 0xEF};
    char buf[10];
    memset(buf, 'G', sizeof(buf));
    // 8 byte window, needs 9, so the output is empty. buf[8] is the guard.
    portalDiagFormatMacSuffix(mac, buf, 8);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('G', buf[1]);
    TEST_ASSERT_EQUAL_CHAR('G', buf[8]);
    // Exactly 9 bytes works.
    portalDiagFormatMacSuffix(mac, buf, 9);
    TEST_ASSERT_EQUAL_STRING("ab:cd:ef", buf);
    TEST_ASSERT_EQUAL_CHAR('G', buf[9]);
    // Zero size and NULL out are harmless.
    TEST_ASSERT_NULL(portalDiagFormatMacSuffix(mac, NULL, 0));
}

// ---- Test 6: interval, wrap-safe ----------------------------------------

void test_interval_basic(void) {
    TEST_ASSERT_FALSE(portalDiagIntervalElapsed(10999, 1000, 10000));
    TEST_ASSERT_TRUE(portalDiagIntervalElapsed(11000, 1000, 10000));
    TEST_ASSERT_TRUE(portalDiagIntervalElapsed(12000, 1000, 10000));
}

void test_interval_wraps_32_bit(void) {
    TEST_ASSERT_TRUE(portalDiagIntervalElapsed(0x00000100u, 0xFFFFFF00u, 0x200u));
    TEST_ASSERT_FALSE(portalDiagIntervalElapsed(0x000000FFu, 0xFFFFFF00u, 0x200u));
}

// ---- Test 7: status names ------------------------------------------------

void test_status_names(void) {
    TEST_ASSERT_EQUAL_STRING("WL_IDLE_STATUS", portalDiagWifiStatusName(0));
    TEST_ASSERT_EQUAL_STRING("WL_NO_SSID_AVAIL", portalDiagWifiStatusName(1));
    TEST_ASSERT_EQUAL_STRING("WL_SCAN_COMPLETED", portalDiagWifiStatusName(2));
    TEST_ASSERT_EQUAL_STRING("WL_CONNECTED", portalDiagWifiStatusName(3));
    TEST_ASSERT_EQUAL_STRING("WL_CONNECT_FAILED", portalDiagWifiStatusName(4));
    TEST_ASSERT_EQUAL_STRING("WL_CONNECTION_LOST", portalDiagWifiStatusName(5));
    TEST_ASSERT_EQUAL_STRING("WL_DISCONNECTED", portalDiagWifiStatusName(6));
    TEST_ASSERT_EQUAL_STRING("WL_STOPPED", portalDiagWifiStatusName(254));
    TEST_ASSERT_EQUAL_STRING("WL_NO_SHIELD", portalDiagWifiStatusName(255));
    TEST_ASSERT_EQUAL_STRING("WL_UNKNOWN", portalDiagWifiStatusName(7));
    TEST_ASSERT_EQUAL_STRING("WL_UNKNOWN", portalDiagWifiStatusName(100));
    TEST_ASSERT_EQUAL_STRING("WL_UNKNOWN", portalDiagWifiStatusName(-1));
}

// ---- Test 8: failure statuses --------------------------------------------

void test_failure_statuses(void) {
    TEST_ASSERT_TRUE(portalDiagIsCredentialTestFailureStatus(1));
    TEST_ASSERT_TRUE(portalDiagIsCredentialTestFailureStatus(4));
    TEST_ASSERT_TRUE(portalDiagIsCredentialTestFailureStatus(5));
    const int others[] = {0, 2, 3, 6, 254, 255, -1};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i) {
        TEST_ASSERT_FALSE(portalDiagIsCredentialTestFailureStatus(others[i]));
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_cuts_query_string);
    RUN_TEST(test_sanitize_cuts_fragment);
    RUN_TEST(test_sanitize_null_and_empty);
    RUN_TEST(test_sanitize_replaces_hostile_bytes);
    RUN_TEST(test_sanitize_exactly_64_unchanged);
    RUN_TEST(test_sanitize_65_and_200_are_capped);
    RUN_TEST(test_sanitize_tiny_buffer_and_guard);
    RUN_TEST(test_sanitize_zero_size_null_out_is_noop);
    RUN_TEST(test_sanitize_size_one_gives_empty);
    RUN_TEST(test_probe_paths_true);
    RUN_TEST(test_probe_paths_false);
    RUN_TEST(test_mac_suffix_last_three_bytes);
    RUN_TEST(test_mac_suffix_null_mac);
    RUN_TEST(test_mac_suffix_small_buffer);
    RUN_TEST(test_interval_basic);
    RUN_TEST(test_interval_wraps_32_bit);
    RUN_TEST(test_status_names);
    RUN_TEST(test_failure_statuses);
    return UNITY_END();
}

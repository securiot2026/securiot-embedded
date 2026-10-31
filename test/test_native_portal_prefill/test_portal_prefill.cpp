#include <string.h>

#include <string>

#include <unity.h>

#include "portal_prefill.h"

void setUp(void) {}
void tearDown(void) {}

static std::string repeatChar(char c, size_t n) {
    return std::string(n, c);
}

// Builds "http://" followed by filler so the total length is exactly `total`.
static std::string urlOfLength(size_t total) {
    const std::string scheme = "http://";
    return scheme + repeatChar('a', total - scheme.size());
}

// ---- Test 1: escape ------------------------------------------------------

void test_escape_special_characters(void) {
    TEST_ASSERT_EQUAL_STRING("a&quot;b&lt;c&gt;&amp;&#39;d",
                             htmlEscapeAttribute("a\"b<c>&'d").c_str());
}

void test_escape_null_is_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", htmlEscapeAttribute(NULL).c_str());
}

void test_escape_drops_control_characters(void) {
    TEST_ASSERT_EQUAL_STRING("ab", htmlEscapeAttribute("a\r\n\tb\x7F").c_str());
}

void test_escape_plain_text_unchanged(void) {
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:5000/x?a=b",
                             htmlEscapeAttribute("http://192.168.1.50:5000/x?a=b").c_str());
}

void test_escape_high_bytes_pass_through(void) {
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", htmlEscapeAttribute("caf\xC3\xA9").c_str());
}

// ---- Test 2: edge URL ----------------------------------------------------

void test_url_accepts_valid(void) {
    TEST_ASSERT_TRUE(isValidPrefillEdgeUrl("http://192.168.1.50:5000"));
    TEST_ASSERT_TRUE(isValidPrefillEdgeUrl("https://edge.example.com"));
    TEST_ASSERT_TRUE(isValidPrefillEdgeUrl("HTTP://192.168.1.50:5000"));
    TEST_ASSERT_TRUE(isValidPrefillEdgeUrl("HtTpS://edge.example.com"));
}

void test_url_rejects_null_empty_and_bare_scheme(void) {
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl(NULL));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl(""));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("https://"));
}

void test_url_rejects_bad_schemes(void) {
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("javascript:alert(1)"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("ftp://host"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("//host"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("data:text/html,x"));
}

void test_url_rejects_hostile_characters(void) {
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a\"b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a'b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a<b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a>b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a\\b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a`b"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://a\r\nb"));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl("http://caf\xC3\xA9"));
}

void test_url_length_boundary(void) {
    TEST_ASSERT_TRUE(isValidPrefillEdgeUrl(urlOfLength(200).c_str()));
    TEST_ASSERT_FALSE(isValidPrefillEdgeUrl(urlOfLength(201).c_str()));
}

// ---- Test 3: device key --------------------------------------------------

void test_key_accepts_printable_ascii(void) {
    TEST_ASSERT_TRUE(isValidPrefillDeviceKey("s3cr3t-Key_value.1"));
    TEST_ASSERT_TRUE(isValidPrefillDeviceKey("\"><script>"));
    TEST_ASSERT_TRUE(isValidPrefillDeviceKey(repeatChar('k', 128).c_str()));
}

void test_key_rejects_null_empty_and_too_long(void) {
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey(NULL));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey(""));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey(repeatChar('k', 129).c_str()));
}

void test_key_rejects_whitespace_and_non_ascii(void) {
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("a b"));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("a\tb"));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("a\rb"));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("a\nb"));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("caf\xC3\xA9"));
    TEST_ASSERT_FALSE(isValidPrefillDeviceKey("a\x7F"));
}

// ---- Test 4: sanitize ----------------------------------------------------

void test_sanitize_all_valid(void) {
    PortalPrefill p = sanitizePortalPrefill("http://192.168.1.50:5000", "key-123",
                                            "3f2b8c1e-5d4a-4e7b-9c1d-0a1b2c3d4e5f", "zone-01");
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:5000", p.edgeApiUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("key-123", p.deviceKey.c_str());
    TEST_ASSERT_EQUAL_STRING("3f2b8c1e-5d4a-4e7b-9c1d-0a1b2c3d4e5f", p.deviceId.c_str());
    TEST_ASSERT_EQUAL_STRING("zone-01", p.zoneId.c_str());
    TEST_ASSERT_TRUE(p.any());
    TEST_ASSERT_EQUAL_INT(4, p.count());
}

void test_sanitize_invalid_device_id_is_emptied_others_stay(void) {
    const char *bad[] = {"has space", "a/b", "\"><script>"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        PortalPrefill p =
            sanitizePortalPrefill("http://edge.local:5000", "key-123", bad[i], "zone-01");
        TEST_ASSERT_EQUAL_STRING("", p.deviceId.c_str());
        TEST_ASSERT_EQUAL_STRING("http://edge.local:5000", p.edgeApiUrl.c_str());
        TEST_ASSERT_EQUAL_STRING("key-123", p.deviceKey.c_str());
        TEST_ASSERT_EQUAL_STRING("zone-01", p.zoneId.c_str());
        TEST_ASSERT_EQUAL_INT(3, p.count());
    }
}

void test_sanitize_javascript_url_is_emptied(void) {
    PortalPrefill p = sanitizePortalPrefill("javascript:alert(1)", "key-123", "dev-1", "zone-01");
    TEST_ASSERT_EQUAL_STRING("", p.edgeApiUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("key-123", p.deviceKey.c_str());
    TEST_ASSERT_EQUAL_STRING("dev-1", p.deviceId.c_str());
    TEST_ASSERT_EQUAL_STRING("zone-01", p.zoneId.c_str());
    TEST_ASSERT_EQUAL_INT(3, p.count());
}

void test_sanitize_all_null(void) {
    PortalPrefill p = sanitizePortalPrefill(NULL, NULL, NULL, NULL);
    TEST_ASSERT_FALSE(p.any());
    TEST_ASSERT_EQUAL_INT(0, p.count());
}

// ---- Test 5: value attribute --------------------------------------------

void test_value_attribute_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", valueAttribute("").c_str());
}

void test_value_attribute_plain(void) {
    TEST_ASSERT_EQUAL_STRING(" value=\"abc\"", valueAttribute("abc").c_str());
}

void test_value_attribute_hostile_key_is_neutralised(void) {
    const std::string key = "x\"onfocus=\"alert(1)";
    TEST_ASSERT_TRUE(isValidPrefillDeviceKey(key.c_str()));
    const std::string out = valueAttribute(key);
    TEST_ASSERT_EQUAL_INT(0, out.compare(0, 8, " value=\""));
    TEST_ASSERT_EQUAL_CHAR('"', out[out.size() - 1]);
    TEST_ASSERT_TRUE(out.find("&quot;") != std::string::npos);
    // Between the delimiters there must be no raw double quote.
    const std::string inner = out.substr(8, out.size() - 9);
    TEST_ASSERT_TRUE(inner.find('"') == std::string::npos);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_escape_special_characters);
    RUN_TEST(test_escape_null_is_empty);
    RUN_TEST(test_escape_drops_control_characters);
    RUN_TEST(test_escape_plain_text_unchanged);
    RUN_TEST(test_escape_high_bytes_pass_through);
    RUN_TEST(test_url_accepts_valid);
    RUN_TEST(test_url_rejects_null_empty_and_bare_scheme);
    RUN_TEST(test_url_rejects_bad_schemes);
    RUN_TEST(test_url_rejects_hostile_characters);
    RUN_TEST(test_url_length_boundary);
    RUN_TEST(test_key_accepts_printable_ascii);
    RUN_TEST(test_key_rejects_null_empty_and_too_long);
    RUN_TEST(test_key_rejects_whitespace_and_non_ascii);
    RUN_TEST(test_sanitize_all_valid);
    RUN_TEST(test_sanitize_invalid_device_id_is_emptied_others_stay);
    RUN_TEST(test_sanitize_javascript_url_is_emptied);
    RUN_TEST(test_sanitize_all_null);
    RUN_TEST(test_value_attribute_empty);
    RUN_TEST(test_value_attribute_plain);
    RUN_TEST(test_value_attribute_hostile_key_is_neutralised);
    return UNITY_END();
}

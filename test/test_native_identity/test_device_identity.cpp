#include <string.h>
#include <unity.h>

#include "device_identity.h"

void setUp(void) {}
void tearDown(void) {}

void test_accepts_uuid_v4(void) {
    TEST_ASSERT_TRUE(isValidIdentityToken("3f2b8c1e-5d4a-4e7b-9c1d-0a1b2c3d4e5f"));
}

void test_accepts_legacy_placeholders(void) {
    TEST_ASSERT_TRUE(isValidIdentityToken("esp32cam-01"));
    TEST_ASSERT_TRUE(isValidIdentityToken("zone-01"));
}

void test_accepts_dot_and_underscore(void) {
    TEST_ASSERT_TRUE(isValidIdentityToken("cam.north_1"));
}

void test_length_boundary(void) {
    char ok[IDENTITY_TOKEN_MAX_LEN + 2];
    memset(ok, 'a', sizeof(ok));
    ok[64] = '\0';
    TEST_ASSERT_TRUE(isValidIdentityToken(ok));
    ok[64] = 'a';
    ok[65] = '\0';
    TEST_ASSERT_FALSE(isValidIdentityToken(ok));
}

void test_rejects_null_and_empty(void) {
    TEST_ASSERT_FALSE(isValidIdentityToken(NULL));
    TEST_ASSERT_FALSE(isValidIdentityToken(""));
}

void test_rejects_whitespace(void) {
    TEST_ASSERT_FALSE(isValidIdentityToken("a b"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\tb"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\rb"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\nb"));
}

void test_rejects_multipart_breakout_characters(void) {
    TEST_ASSERT_FALSE(isValidIdentityToken("a\"b"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a/b"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a;b"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\\b"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\r\n--boundary"));
}

void test_rejects_non_ascii(void) {
    TEST_ASSERT_FALSE(isValidIdentityToken("caf\xC3\xA9"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\x80"));
    TEST_ASSERT_FALSE(isValidIdentityToken("a\xFF"));
}

void test_resolve_returns_stored_when_valid(void) {
    TEST_ASSERT_EQUAL_STRING("stored-id", resolveIdentity("stored-id", "fallback"));
}

void test_resolve_falls_back_when_null_empty_or_invalid(void) {
    TEST_ASSERT_EQUAL_STRING("fallback", resolveIdentity(NULL, "fallback"));
    TEST_ASSERT_EQUAL_STRING("fallback", resolveIdentity("", "fallback"));
    TEST_ASSERT_EQUAL_STRING("fallback", resolveIdentity("bad id", "fallback"));
    TEST_ASSERT_EQUAL_STRING("fallback", resolveIdentity("x\r\ny", "fallback"));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_accepts_uuid_v4);
    RUN_TEST(test_accepts_legacy_placeholders);
    RUN_TEST(test_accepts_dot_and_underscore);
    RUN_TEST(test_length_boundary);
    RUN_TEST(test_rejects_null_and_empty);
    RUN_TEST(test_rejects_whitespace);
    RUN_TEST(test_rejects_multipart_breakout_characters);
    RUN_TEST(test_rejects_non_ascii);
    RUN_TEST(test_resolve_returns_stored_when_valid);
    RUN_TEST(test_resolve_falls_back_when_null_empty_or_invalid);
    return UNITY_END();
}

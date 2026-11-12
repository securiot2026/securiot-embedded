#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include <unity.h>

#include "log_ring.h"

void setUp(void) {}
void tearDown(void) {}

// Pushes a C string into the ring.
static void pushStr(LogRing &r, const char *s) {
    logRingPush(r, (const uint8_t *)s, strlen(s));
}

// Copies everything readable from the cursor (up to 4096 bytes) into a string.
static std::string readAll(const LogRing &r, uint32_t cursor) {
    uint8_t tmp[4096];
    size_t n = logRingCopy(r, cursor, tmp, sizeof(tmp));
    return std::string((const char *)tmp, n);
}

// ---- Constants ---------------------------------------------------------------

void test_constants(void) {
    TEST_ASSERT_EQUAL_INT(23, LOG_MIRROR_PORT);
    TEST_ASSERT_EQUAL_INT(2048, LOG_MIRROR_RING_BYTES);
    TEST_ASSERT_EQUAL_INT(0, LOG_MIRROR_RING_BYTES & (LOG_MIRROR_RING_BYTES - 1));
    TEST_ASSERT_EQUAL_INT(128, LOG_MIRROR_CHUNK_BYTES);
    TEST_ASSERT_TRUE(LOG_MIRROR_CHUNK_BYTES <= LOG_MIRROR_RING_BYTES);
    TEST_ASSERT_EQUAL_INT(4, LOG_MIRROR_MAX_CHUNKS_PER_POLL);
}

// ---- Init --------------------------------------------------------------------

void test_init_accepts_power_of_two(void) {
    uint8_t storage[16];
    LogRing r;
    TEST_ASSERT_TRUE(logRingInit(r, storage, 16));
    TEST_ASSERT_EQUAL_UINT32(16, r.cap);
    TEST_ASSERT_EQUAL_UINT32(0, r.head);
    TEST_ASSERT_EQUAL_UINT32(0, r.stored);
}

void test_init_rejects_bad_capacity_and_null_storage(void) {
    uint8_t storage[16];
    const uint32_t bad[] = {0, 3, 1000};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        LogRing r;
        TEST_ASSERT_FALSE(logRingInit(r, storage, bad[i]));
        pushStr(r, "abc");
        TEST_ASSERT_EQUAL_UINT32(0, r.head);
        uint8_t out[8];
        TEST_ASSERT_EQUAL_UINT32(0, logRingCopy(r, 0, out, sizeof(out)));
    }
    LogRing r;
    TEST_ASSERT_FALSE(logRingInit(r, NULL, 16));
    pushStr(r, "abc");
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT32(0, logRingCopy(r, 0, out, sizeof(out)));
}

// ---- Basic push and copy -------------------------------------------------------

void test_basic_push_and_copy(void) {
    uint8_t storage[16];
    LogRing r;
    logRingInit(r, storage, 16);
    pushStr(r, "abc");
    pushStr(r, "def");
    uint32_t dropped = 99;
    uint32_t c = logRingClamp(r, 0, &dropped);
    TEST_ASSERT_EQUAL_UINT32(0, c);
    TEST_ASSERT_EQUAL_UINT32(0, dropped);
    TEST_ASSERT_EQUAL_STRING("abcdef", readAll(r, c).c_str());
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT32(6, logRingCopy(r, c, out, sizeof(out)));
}

void test_partial_consume(void) {
    uint8_t storage[16];
    LogRing r;
    logRingInit(r, storage, 16);
    pushStr(r, "abcdef");
    uint32_t cursor = 0;
    uint8_t out[8];
    size_t n = logRingCopy(r, cursor, out, 4);
    TEST_ASSERT_EQUAL_UINT32(4, n);
    TEST_ASSERT_EQUAL_MEMORY("abcd", out, 4);
    cursor += (uint32_t)n;
    n = logRingCopy(r, cursor, out, 4);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_MEMORY("ef", out, 2);
    cursor += (uint32_t)n;
    TEST_ASSERT_EQUAL_UINT32(0, logRingCopy(r, cursor, out, 4));
}

// ---- Wrap and drop -------------------------------------------------------------

void test_wrap_and_drop(void) {
    uint8_t storage[16];
    LogRing r;
    logRingInit(r, storage, 16);
    pushStr(r, "0123456789");
    pushStr(r, "ABCDEFGHIJ");
    TEST_ASSERT_EQUAL_UINT32(20, r.head);
    TEST_ASSERT_EQUAL_UINT32(16, r.stored);
    TEST_ASSERT_EQUAL_UINT32(4, logRingOldest(r));
    uint32_t dropped = 0;
    uint32_t c = logRingClamp(r, 0, &dropped);
    TEST_ASSERT_EQUAL_UINT32(4, c);
    TEST_ASSERT_EQUAL_UINT32(4, dropped);
    // Bytes 4..19 are "456789" followed by "ABCDEFGHIJ".
    TEST_ASSERT_EQUAL_STRING("456789ABCDEFGHIJ", readAll(r, c).c_str());
}

void test_oversized_push_keeps_newest(void) {
    uint8_t storage[8];
    LogRing r;
    logRingInit(r, storage, 8);
    pushStr(r, "0123456789ABCDEFGHIJ");
    TEST_ASSERT_EQUAL_UINT32(20, r.head);
    TEST_ASSERT_EQUAL_UINT32(8, r.stored);
    TEST_ASSERT_EQUAL_STRING("CDEFGHIJ", readAll(r, logRingOldest(r)).c_str());
}

void test_exact_fill_then_one_more(void) {
    uint8_t storage[8];
    LogRing r;
    logRingInit(r, storage, 8);
    pushStr(r, "01234567");
    uint32_t dropped = 99;
    uint32_t c = logRingClamp(r, 0, &dropped);
    TEST_ASSERT_EQUAL_UINT32(0, c);
    TEST_ASSERT_EQUAL_UINT32(0, dropped);
    TEST_ASSERT_EQUAL_STRING("01234567", readAll(r, c).c_str());
    pushStr(r, "8");
    c = logRingClamp(r, 0, &dropped);
    TEST_ASSERT_EQUAL_UINT32(1, c);
    TEST_ASSERT_EQUAL_UINT32(1, dropped);
    TEST_ASSERT_EQUAL_STRING("12345678", readAll(r, c).c_str());
}

void test_empty_and_null_pushes_change_nothing(void) {
    uint8_t storage[8];
    LogRing r;
    logRingInit(r, storage, 8);
    pushStr(r, "ab");
    logRingPush(r, (const uint8_t *)"zzz", 0);
    logRingPush(r, NULL, 5);
    TEST_ASSERT_EQUAL_UINT32(2, r.head);
    TEST_ASSERT_EQUAL_UINT32(2, r.stored);
    TEST_ASSERT_EQUAL_STRING("ab", readAll(r, 0).c_str());
}

// ---- History start -------------------------------------------------------------

void test_history_start_without_wrap_is_oldest(void) {
    uint8_t storage[32];
    LogRing r;
    logRingInit(r, storage, 32);
    pushStr(r, "one\r\ntwo\r\n");
    TEST_ASSERT_EQUAL_UINT32(0, logRingHistoryStart(r));
    TEST_ASSERT_EQUAL_UINT32(logRingOldest(r), logRingHistoryStart(r));
}

void test_history_start_after_wrap_is_after_first_newline(void) {
    uint8_t storage[32];
    LogRing r;
    logRingInit(r, storage, 32);
    // 6 lines of 8 bytes each: 48 bytes pushed, the window keeps the last 32.
    pushStr(r, "line01\r\n");
    pushStr(r, "line02\r\n");
    pushStr(r, "line03\r\n");
    pushStr(r, "line04\r\n");
    pushStr(r, "line05\r\n");
    pushStr(r, "line06\r\n");
    TEST_ASSERT_EQUAL_UINT32(48, r.head);
    TEST_ASSERT_EQUAL_UINT32(16, logRingOldest(r));
    // The window starts exactly on "line03", so the first newline ends line03.
    uint32_t start = logRingHistoryStart(r);
    TEST_ASSERT_EQUAL_UINT32(24, start);
    TEST_ASSERT_EQUAL_STRING("line04\r\nline05\r\nline06\r\n", readAll(r, start).c_str());
}

void test_history_start_mid_line_window(void) {
    uint8_t storage[32];
    LogRing r;
    logRingInit(r, storage, 32);
    pushStr(r, "abcdefghijklmnopqrst\n");   // 21 bytes
    pushStr(r, "uvwxyz0123456789\n");        // 17 bytes, 38 total, window drops 6
    uint32_t start = logRingHistoryStart(r);
    std::string rest = readAll(r, start);
    TEST_ASSERT_EQUAL_STRING("uvwxyz0123456789\n", rest.c_str());
}

void test_history_start_without_newline_is_oldest(void) {
    uint8_t storage[16];
    LogRing r;
    logRingInit(r, storage, 16);
    pushStr(r, "0123456789012345678901234");
    TEST_ASSERT_EQUAL_UINT32(logRingOldest(r), logRingHistoryStart(r));
}

// ---- 32-bit counter wrap -------------------------------------------------------

void test_counter_wrap(void) {
    uint8_t storage[16];
    LogRing r;
    logRingInit(r, storage, 16);
    r.head = 0xFFFFFFF8u;
    r.stored = 0;
    uint32_t cursor = r.head;
    pushStr(r, "0123456789ABCDEF");
    TEST_ASSERT_EQUAL_UINT32(8, r.head);
    TEST_ASSERT_EQUAL_UINT32(16, r.stored);
    uint32_t oldest = logRingOldest(r);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF8u, oldest);
    TEST_ASSERT_EQUAL_STRING("0123456789ABCDEF", readAll(r, oldest).c_str());
    pushStr(r, "WXYZ");
    uint32_t dropped = 0;
    uint32_t c = logRingClamp(r, cursor, &dropped);
    TEST_ASSERT_EQUAL_UINT32(4, dropped);
    TEST_ASSERT_EQUAL_UINT32(logRingOldest(r), c);
}

// ---- Producer and consumer ------------------------------------------------------

void test_producer_consumer_loses_nothing(void) {
    uint8_t storage[256];
    LogRing r;
    logRingInit(r, storage, 256);
    std::string in;
    std::string out;
    uint32_t cursor = 0;
    uint32_t seed = 12345;
    for (int i = 0; i < 400; i++) {
        seed = seed * 1103515245u + 12345u;
        size_t len = 1 + ((seed >> 16) % 40);
        uint8_t chunk[40];
        for (size_t k = 0; k < len; k++) {
            chunk[k] = (uint8_t)('a' + ((i + k) % 26));
        }
        logRingPush(r, chunk, len);
        in.append((const char *)chunk, len);
        uint32_t dropped = 0;
        cursor = logRingClamp(r, cursor, &dropped);
        TEST_ASSERT_EQUAL_UINT32(0, dropped);
        uint8_t buf[24];
        size_t n = logRingCopy(r, cursor, buf, sizeof(buf));
        out.append((const char *)buf, n);
        cursor += (uint32_t)n;
    }
    // Drain what is left.
    for (;;) {
        uint8_t buf[24];
        size_t n = logRingCopy(r, cursor, buf, sizeof(buf));
        if (n == 0) break;
        out.append((const char *)buf, n);
        cursor += (uint32_t)n;
    }
    TEST_ASSERT_TRUE(in == out);
}

void test_stalled_consumer_drops_exactly_the_overflow(void) {
    uint8_t storage[256];
    LogRing r;
    logRingInit(r, storage, 256);
    uint32_t cursor = 0;
    // The consumer reads 50 bytes first, then stops reading.
    for (int i = 0; i < 50; i++) {
        uint8_t b = (uint8_t)('A' + (i % 26));
        logRingPush(r, &b, 1);
    }
    uint8_t buf[64];
    size_t consumed = logRingCopy(r, cursor, buf, 50);
    TEST_ASSERT_EQUAL_UINT32(50, consumed);
    cursor += (uint32_t)consumed;
    // 600 bytes pushed while the consumer is away.
    std::string pushed;
    for (int i = 0; i < 600; i++) {
        uint8_t b = (uint8_t)('a' + (i % 26));
        logRingPush(r, &b, 1);
        pushed.push_back((char)b);
    }
    uint32_t dropped = 0;
    uint32_t c = logRingClamp(r, cursor, &dropped);
    TEST_ASSERT_EQUAL_UINT32(600 - 256, dropped);
    TEST_ASSERT_TRUE(dropped <= 600);
    std::string tail = readAll(r, c);
    TEST_ASSERT_EQUAL_UINT32(256, tail.size());
    TEST_ASSERT_TRUE(tail == pushed.substr(600 - 256));
}

// ---- Formatters ----------------------------------------------------------------

void test_skipped_line_exact(void) {
    char out[64];
    int n = logFormatSkippedLine(out, sizeof(out), 12);
    TEST_ASSERT_EQUAL_STRING("\r\n[log mirror: skipped 12 bytes]\r\n", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), n);
    TEST_ASSERT_NULL(strstr(out, "://"));
    TEST_ASSERT_NULL(strchr(out, '?'));
    TEST_ASSERT_NULL(strchr(out, '%'));
}

void test_skipped_line_buffer_contract(void) {
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    logFormatSkippedLine(buf, 16, 12);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
    char guard[4];
    memset(guard, 'x', sizeof(guard));
    TEST_ASSERT_EQUAL_INT(0, logFormatSkippedLine(NULL, 16, 12));
    TEST_ASSERT_EQUAL_INT(0, logFormatSkippedLine(guard, 0, 12));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)guard[0]);
}

void test_listening_line_exact_and_contract(void) {
    char out[64];
    logFormatListeningLog(out, sizeof(out), 23);
    TEST_ASSERT_EQUAL_STRING("Log mirror: listening on port 23", out);
    char buf[17];
    memset(buf, 'x', sizeof(buf));
    logFormatListeningLog(buf, 16, 23);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);
    char guard[4];
    memset(guard, 'x', sizeof(guard));
    TEST_ASSERT_EQUAL_INT(0, logFormatListeningLog(NULL, 16, 23));
    TEST_ASSERT_EQUAL_INT(0, logFormatListeningLog(guard, 0, 23));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)guard[0]);
}

static bool hasDigit(const char *s) {
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') return true;
    }
    return false;
}

void test_fixed_texts_are_clean(void) {
    TEST_ASSERT_EQUAL_STRING("Log mirror: client connected", LOG_MIRROR_LOG_CONNECTED);
    TEST_ASSERT_EQUAL_STRING("Log mirror: client disconnected", LOG_MIRROR_LOG_DISCONNECTED);
    const char *texts[] = {LOG_MIRROR_LOG_CONNECTED, LOG_MIRROR_LOG_DISCONNECTED, LOG_MIRROR_BANNER};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        TEST_ASSERT_FALSE(hasDigit(texts[i]));
        TEST_ASSERT_NULL(strstr(texts[i], "://"));
        TEST_ASSERT_NULL(strchr(texts[i], '?'));
    }
    size_t bl = strlen(LOG_MIRROR_BANNER);
    TEST_ASSERT_TRUE(bl >= 2);
    TEST_ASSERT_EQUAL_UINT8('\r', (uint8_t)LOG_MIRROR_BANNER[bl - 2]);
    TEST_ASSERT_EQUAL_UINT8('\n', (uint8_t)LOG_MIRROR_BANNER[bl - 1]);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_constants);
    RUN_TEST(test_init_accepts_power_of_two);
    RUN_TEST(test_init_rejects_bad_capacity_and_null_storage);
    RUN_TEST(test_basic_push_and_copy);
    RUN_TEST(test_partial_consume);
    RUN_TEST(test_wrap_and_drop);
    RUN_TEST(test_oversized_push_keeps_newest);
    RUN_TEST(test_exact_fill_then_one_more);
    RUN_TEST(test_empty_and_null_pushes_change_nothing);
    RUN_TEST(test_history_start_without_wrap_is_oldest);
    RUN_TEST(test_history_start_after_wrap_is_after_first_newline);
    RUN_TEST(test_history_start_mid_line_window);
    RUN_TEST(test_history_start_without_newline_is_oldest);
    RUN_TEST(test_counter_wrap);
    RUN_TEST(test_producer_consumer_loses_nothing);
    RUN_TEST(test_stalled_consumer_drops_exactly_the_overflow);
    RUN_TEST(test_skipped_line_exact);
    RUN_TEST(test_skipped_line_buffer_contract);
    RUN_TEST(test_listening_line_exact_and_contract);
    RUN_TEST(test_fixed_texts_are_clean);
    return UNITY_END();
}

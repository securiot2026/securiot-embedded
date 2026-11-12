#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "button_state.h"

void setUp(void) {}
void tearDown(void) {}

static ButtonStatus step(ButtonTracker &t, uint32_t nowMs, bool pressed) {
    return buttonUpdate(t, nowMs, pressed);
}

// ---- Constants ---------------------------------------------------------------

void test_button_constants(void) {
    TEST_ASSERT_EQUAL_INT(30, BUTTON_DEBOUNCE_MS);
    TEST_ASSERT_EQUAL_INT(500, BUTTON_COUNTDOWN_START_MS);
    TEST_ASSERT_EQUAL_INT(3000, BUTTON_LONG_PRESS_MS);
    TEST_ASSERT_EQUAL_INT(800, BUTTON_RESET_MIN_CONFIRM_MS);
    TEST_ASSERT_EQUAL_INT(5000, BUTTON_RESET_MAX_WAIT_MS);
}

// ---- Idle ----------------------------------------------------------------------

void test_idle_stays_idle(void) {
    ButtonTracker t;
    for (uint32_t now = 0; now <= 1000; now += 10) {
        ButtonStatus s = step(t, now, false);
        TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
        TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
        TEST_ASSERT_EQUAL_UINT32(0, s.heldMs);
    }
}

// ---- Short press ---------------------------------------------------------------

void test_clean_short_press(void) {
    ButtonTracker t;
    step(t, 990, false);
    ButtonStatus s = step(t, 1000, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1010, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1020, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1030, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);

    step(t, 1100, true);
    s = step(t, 1200, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 1210, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 1220, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 1230, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, s.event);
    TEST_ASSERT_EQUAL_UINT32(200, s.heldMs);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);

    s = step(t, 1240, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
}

// ---- Bounce --------------------------------------------------------------------

void test_press_bounce_is_filtered(void) {
    ButtonTracker t;
    step(t, 1000, false);
    // Raw toggles every 10 ms, never 30 ms in a row.
    ButtonStatus s = step(t, 1010, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1020, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1030, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1040, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1050, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    // The last edge is the press at 1060.
    s = step(t, 1060, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1070, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1080, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    s = step(t, 1090, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);
    TEST_ASSERT_EQUAL_UINT32(30, s.heldMs);  // hold time starts at 1060
}

void test_hold_bounce_changes_nothing(void) {
    ButtonTracker t;
    step(t, 1000, false);
    step(t, 1010, true);
    step(t, 1040, true);  // accepted, press edge 1010
    ButtonStatus s = step(t, 1520, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_COUNTDOWN, s.phase);
    TEST_ASSERT_EQUAL_UINT32(510, s.heldMs);

    s = step(t, 1530, false);  // 10 ms glitch
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 1540, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_COUNTDOWN, s.phase);
    TEST_ASSERT_EQUAL_UINT32(530, s.heldMs);  // 1540 - 1010, unbroken
}

// ---- Long press ----------------------------------------------------------------

void test_long_press_timing_and_single_event(void) {
    ButtonTracker t;
    step(t, 1990, false);
    int longEvents = 0;
    for (uint32_t now = 2000; now <= 5000; now += 10) {
        ButtonStatus s = step(t, now, true);
        TEST_ASSERT_TRUE(s.event == BUTTON_EVENT_NONE || s.event == BUTTON_EVENT_LONG_PRESS);
        if (s.event == BUTTON_EVENT_LONG_PRESS) {
            longEvents++;
            TEST_ASSERT_EQUAL_UINT32(5000, now);
            TEST_ASSERT_EQUAL_UINT32(3000, s.heldMs);
            TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_CONFIRMED, s.phase);
            continue;
        }
        if (now < 2030) {
            TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
            continue;
        }
        uint32_t held = now - 2000;
        TEST_ASSERT_EQUAL_UINT32(held, s.heldMs);
        if (held < 500) {
            TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);
        } else {
            TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_COUNTDOWN, s.phase);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, longEvents);

    ButtonStatus s = step(t, 5010, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_CONFIRMED, s.phase);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);

    // Release after a long press: no SHORT_PRESS.
    s = step(t, 5020, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_CONFIRMED, s.phase);
    s = step(t, 5050, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
}

void test_just_under_long_is_short(void) {
    ButtonTracker t;
    step(t, 1990, false);
    step(t, 2000, true);
    bool sawLong = false;
    ButtonStatus s;
    for (uint32_t now = 2010; now < 4990; now += 10) {
        s = step(t, now, true);
        if (s.event == BUTTON_EVENT_LONG_PRESS) sawLong = true;
    }
    // Raw release first seen at 4990 (held 2990).
    s = step(t, 4990, false);
    if (s.event == BUTTON_EVENT_LONG_PRESS) sawLong = true;
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 5000, false);
    if (s.event == BUTTON_EVENT_LONG_PRESS) sawLong = true;
    s = step(t, 5010, false);
    if (s.event == BUTTON_EVENT_LONG_PRESS) sawLong = true;
    s = step(t, 5020, false);
    TEST_ASSERT_FALSE(sawLong);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, s.event);
    TEST_ASSERT_EQUAL_UINT32(2990, s.heldMs);
}

// ---- Boot hold -------------------------------------------------------------------

void test_boot_hold_flag(void) {
    ButtonTracker t;
    ButtonStatus s = step(t, 500, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);
    TEST_ASSERT_TRUE(s.fromBoot);

    int longEvents = 0;
    for (uint32_t now = 510; now <= 3500; now += 10) {
        s = step(t, now, true);
        if (s.event == BUTTON_EVENT_LONG_PRESS) {
            longEvents++;
            TEST_ASSERT_EQUAL_UINT32(3500, now);
            TEST_ASSERT_TRUE(s.fromBoot);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, longEvents);

    // Release, then a fresh press is not a boot hold.
    step(t, 3510, false);
    s = step(t, 3540, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_IDLE, s.phase);
    step(t, 3600, true);
    step(t, 3620, true);
    s = step(t, 3630, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);
    TEST_ASSERT_FALSE(s.fromBoot);

    // A boot hold released early is a short press that still reports fromBoot.
    ButtonTracker u;
    step(u, 500, true);
    step(u, 600, false);
    step(u, 620, false);
    s = step(u, 630, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, s.event);
    TEST_ASSERT_EQUAL_UINT32(100, s.heldMs);
    TEST_ASSERT_TRUE(s.fromBoot);
}

// ---- Wrap --------------------------------------------------------------------------

void test_wrap_long_press(void) {
    ButtonTracker t;
    const uint32_t edge = 0xFFFFFC00u;
    step(t, edge - 10, false);
    int longEvents = 0;
    uint32_t now = edge;
    for (int i = 0; i <= 320; i++, now += 10) {
        ButtonStatus s = step(t, now, true);
        if (s.event == BUTTON_EVENT_LONG_PRESS) {
            longEvents++;
            TEST_ASSERT_EQUAL_UINT32(3000, (uint32_t)(now - edge));
            TEST_ASSERT_TRUE(now < edge);  // the counter wrapped past zero
        }
    }
    TEST_ASSERT_EQUAL_INT(1, longEvents);
}

void test_wrap_short_press_held_ms(void) {
    ButtonTracker t;
    uint32_t now = 0xFFFFFF00u;
    step(t, now, false);
    now = 0xFFFFFF10u;
    uint32_t edge = now;
    for (int i = 0; i < 5; i++, now += 10) {
        step(t, now, true);
    }
    // Raw release first seen after the wrap.
    uint32_t releaseEdge = 0x00000010u;
    now = releaseEdge;
    ButtonStatus s = step(t, now, false);
    int guard = 0;
    while (s.event == BUTTON_EVENT_NONE && guard++ < 20) {
        now += 10;
        s = step(t, now, false);
    }
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, s.event);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(releaseEdge - edge), s.heldMs);
    TEST_ASSERT_EQUAL_UINT32(256, s.heldMs);
}

// ---- Sparse polling ------------------------------------------------------------------

void test_sparse_polling_no_long_from_release(void) {
    ButtonTracker t;
    step(t, 90, false);
    step(t, 100, true);
    ButtonStatus s = step(t, 130, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_PRESSED, s.phase);

    s = step(t, 9000, false);  // raw released, 8900 ms after the edge
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_NONE, s.event);
    s = step(t, 9030, false);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_SHORT_PRESS, s.event);
    TEST_ASSERT_EQUAL_UINT32(2999, s.heldMs);
}

void test_sparse_polling_continuous_hold_still_fires(void) {
    ButtonTracker t;
    step(t, 90, false);
    step(t, 100, true);
    step(t, 130, true);
    ButtonStatus s = step(t, 5000, true);
    TEST_ASSERT_EQUAL_INT(BUTTON_EVENT_LONG_PRESS, s.event);
    TEST_ASSERT_EQUAL_INT(BUTTON_PHASE_CONFIRMED, s.phase);
}

void test_sixty_second_hold_fires_once(void) {
    ButtonTracker t;
    step(t, 0, false);
    int longEvents = 0;
    for (uint32_t now = 10; now <= 60000; now += 10) {
        ButtonStatus s = step(t, now, true);
        if (s.event == BUTTON_EVENT_LONG_PRESS) longEvents++;
    }
    TEST_ASSERT_EQUAL_INT(1, longEvents);
}

// ---- Log formatter ---------------------------------------------------------------------

void test_event_log_exact_lines(void) {
    char out[160];
    buttonFormatEventLog(out, sizeof(out), BUTTON_EVENT_SHORT_PRESS, 200, false);
    TEST_ASSERT_EQUAL_STRING("Button: short press held_ms=200, no action", out);
    buttonFormatEventLog(out, sizeof(out), BUTTON_EVENT_LONG_PRESS, 3000, false);
    TEST_ASSERT_EQUAL_STRING("Button: long press confirmed held_ms=3000, erasing config and restarting into portal", out);
    buttonFormatEventLog(out, sizeof(out), BUTTON_EVENT_LONG_PRESS, 3000, true);
    TEST_ASSERT_EQUAL_STRING("Button: held at boot, long press confirmed held_ms=3000, erasing config and restarting into portal", out);
}

void test_event_log_none_and_contract(void) {
    char out[160];
    memset(out, 'x', sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, buttonFormatEventLog(out, sizeof(out), BUTTON_EVENT_NONE, 5, false));
    TEST_ASSERT_EQUAL_STRING("", out);

    char buf[17];
    memset(buf, 'x', sizeof(buf));
    buttonFormatEventLog(buf, 16, BUTTON_EVENT_SHORT_PRESS, 200, false);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)buf[15]);
    TEST_ASSERT_EQUAL_UINT32(15, strlen(buf));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)buf[16]);

    char small[4];
    memset(small, 'x', sizeof(small));
    TEST_ASSERT_EQUAL_INT(0, buttonFormatEventLog(NULL, 16, BUTTON_EVENT_SHORT_PRESS, 1, false));
    TEST_ASSERT_EQUAL_INT(0, buttonFormatEventLog(small, 0, BUTTON_EVENT_SHORT_PRESS, 1, false));
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)small[0]);

    const ButtonEvent events[] = {BUTTON_EVENT_SHORT_PRESS, BUTTON_EVENT_LONG_PRESS};
    for (size_t i = 0; i < 2; i++) {
        for (int boot = 0; boot < 2; boot++) {
            buttonFormatEventLog(out, sizeof(out), events[i], 2999, boot != 0);
            TEST_ASSERT_NULL(strstr(out, "http"));
            TEST_ASSERT_NULL(strstr(out, "://"));
            TEST_ASSERT_NULL(strchr(out, '?'));
        }
    }
}

// ---- Post confirm wait -------------------------------------------------------------------

void test_reset_wait_done(void) {
    TEST_ASSERT_FALSE(buttonResetWaitDone(1000, 1000, false));
    TEST_ASSERT_FALSE(buttonResetWaitDone(1799, 1000, false));
    TEST_ASSERT_FALSE(buttonResetWaitDone(1799, 1000, true));
    TEST_ASSERT_TRUE(buttonResetWaitDone(1800, 1000, false));
    TEST_ASSERT_TRUE(buttonResetWaitDone(3000, 1000, false));
    TEST_ASSERT_FALSE(buttonResetWaitDone(3000, 1000, true));
    TEST_ASSERT_FALSE(buttonResetWaitDone(5999, 1000, true));
    TEST_ASSERT_TRUE(buttonResetWaitDone(6000, 1000, true));
    TEST_ASSERT_TRUE(buttonResetWaitDone(60000, 1000, true));
}

void test_reset_wait_done_wrap(void) {
    const uint32_t start = 0xFFFFFF00u;
    TEST_ASSERT_FALSE(buttonResetWaitDone(start + 799u, start, false));
    TEST_ASSERT_TRUE(buttonResetWaitDone(start + 800u, start, false));
    TEST_ASSERT_FALSE(buttonResetWaitDone(start + 4999u, start, true));
    TEST_ASSERT_TRUE(buttonResetWaitDone(start + 5000u, start, true));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_button_constants);
    RUN_TEST(test_idle_stays_idle);
    RUN_TEST(test_clean_short_press);
    RUN_TEST(test_press_bounce_is_filtered);
    RUN_TEST(test_hold_bounce_changes_nothing);
    RUN_TEST(test_long_press_timing_and_single_event);
    RUN_TEST(test_just_under_long_is_short);
    RUN_TEST(test_boot_hold_flag);
    RUN_TEST(test_wrap_long_press);
    RUN_TEST(test_wrap_short_press_held_ms);
    RUN_TEST(test_sparse_polling_no_long_from_release);
    RUN_TEST(test_sparse_polling_continuous_hold_still_fires);
    RUN_TEST(test_sixty_second_hold_fires_once);
    RUN_TEST(test_event_log_exact_lines);
    RUN_TEST(test_event_log_none_and_contract);
    RUN_TEST(test_reset_wait_done);
    RUN_TEST(test_reset_wait_done_wrap);
    return UNITY_END();
}

#include <stdint.h>
#include <string.h>
#include <vector>

#include <unity.h>

#include "alert_state.h"
#include "button_state.h"
#include "led_pattern.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgb kBlack = {0, 0, 0};

static Rgb colorFor(LedSystem system, uint32_t nowMs, ButtonPhase phase = BUTTON_PHASE_IDLE,
                    uint32_t heldMs = 0, Rgb composed = Rgb{0, 255, 0}) {
    LedInput in;
    in.system = system;
    in.button = phase;
    in.heldMs = heldMs;
    in.composed = composed;
    return ledPatternColor(in, nowMs);
}

static std::vector<uint32_t> sequence(LedSystem system) {
    std::vector<uint32_t> seq;
    for (uint32_t t = 0; t <= 5950; t += 50) {
        Rgb c = colorFor(system, t);
        seq.push_back(((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b);
    }
    return seq;
}

// ---- Helpers ---------------------------------------------------------------

void test_blink_on(void) {
    TEST_ASSERT_TRUE(ledBlinkOn(0, 1200));
    TEST_ASSERT_TRUE(ledBlinkOn(599, 1200));
    TEST_ASSERT_FALSE(ledBlinkOn(600, 1200));
    TEST_ASSERT_FALSE(ledBlinkOn(1199, 1200));
    TEST_ASSERT_TRUE(ledBlinkOn(1200, 1200));
    TEST_ASSERT_TRUE(ledBlinkOn(12345, 0));
}

void test_pulse_level(void) {
    TEST_ASSERT_EQUAL_UINT8(51, ledPulseLevel(0, 3000, 51));
    TEST_ASSERT_EQUAL_UINT8(255, ledPulseLevel(1500, 3000, 51));
    TEST_ASSERT_EQUAL_UINT8(153, ledPulseLevel(750, 3000, 51));
    TEST_ASSERT_EQUAL_UINT8(153, ledPulseLevel(2250, 3000, 51));
    for (uint32_t t = 0; t <= 6000; t += 7) {
        uint8_t level = ledPulseLevel(t, 3000, 51);
        TEST_ASSERT_TRUE(level >= 51);
        TEST_ASSERT_TRUE(level <= 255);
    }
}

void test_scale(void) {
    Rgb a = ledScale(Rgb{255, 255, 0}, 51);
    TEST_ASSERT_TRUE((a == Rgb{51, 51, 0}));
    Rgb b = ledScale(Rgb{255, 255, 0}, 255);
    TEST_ASSERT_TRUE((b == Rgb{255, 255, 0}));
    Rgb c = ledScale(Rgb{255, 255, 0}, 0);
    TEST_ASSERT_TRUE((c == Rgb{0, 0, 0}));
}

void test_countdown_period(void) {
    TEST_ASSERT_EQUAL_UINT32(600, ledCountdownPeriodMs(0));
    TEST_ASSERT_EQUAL_UINT32(600, ledCountdownPeriodMs(500));
    TEST_ASSERT_EQUAL_UINT32(120, ledCountdownPeriodMs(3000));
    TEST_ASSERT_EQUAL_UINT32(120, ledCountdownPeriodMs(9999));
    TEST_ASSERT_EQUAL_UINT32(360, ledCountdownPeriodMs(1750));
    uint32_t prev = ledCountdownPeriodMs(500);
    for (uint32_t h = 500; h <= 3000; h += 50) {
        uint32_t p = ledCountdownPeriodMs(h);
        TEST_ASSERT_TRUE(p <= prev);
        prev = p;
    }
}

// ---- System states -----------------------------------------------------------

void test_connecting_blue_blink(void) {
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTING, 0) == Rgb{0, 0, 255}));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTING, 599) == Rgb{0, 0, 255}));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTING, 600) == kBlack));
    for (uint32_t t = 0; t <= 6000; t += 10) {
        Rgb c = colorFor(LED_SYSTEM_CONNECTING, t);
        TEST_ASSERT_TRUE((c == Rgb{0, 0, 255}) || (c == kBlack));
    }
}

void test_portal_ready_pulses_cyan(void) {
    bool varies = false;
    Rgb first = colorFor(LED_SYSTEM_PORTAL_READY, 0);
    for (uint32_t t = 0; t <= 6000; t += 10) {
        Rgb c = colorFor(LED_SYSTEM_PORTAL_READY, t);
        TEST_ASSERT_EQUAL_UINT8(0, c.r);
        TEST_ASSERT_EQUAL_UINT8(c.g, c.b);
        TEST_ASSERT_TRUE(c.g >= 51 && c.g <= 255);
        if (c != first) varies = true;
    }
    TEST_ASSERT_TRUE(varies);
}

void test_portal_client_solid_cyan(void) {
    for (uint32_t t = 0; t <= 6000; t += 37) {
        TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_PORTAL_CLIENT, t) == Rgb{0, 255, 255}));
    }
}

void test_portal_credentials_pulses_yellow(void) {
    bool varies = false;
    Rgb first = colorFor(LED_SYSTEM_PORTAL_CREDENTIALS, 0);
    for (uint32_t t = 0; t <= 6000; t += 10) {
        Rgb c = colorFor(LED_SYSTEM_PORTAL_CREDENTIALS, t);
        TEST_ASSERT_EQUAL_UINT8(0, c.b);
        TEST_ASSERT_EQUAL_UINT8(c.r, c.g);
        TEST_ASSERT_TRUE(c.r >= 51 && c.r <= 255);
        if (c != first) varies = true;
    }
    TEST_ASSERT_TRUE(varies);
}

void test_connected_passthrough_uses_alert_state(void) {
    AlertState s;
    uint32_t now = 1000;
    Outputs idle = computeOutputs(s, now);
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, now, BUTTON_PHASE_IDLE, 0, idle.led) == idle.led));
    TEST_ASSERT_TRUE((idle.led == Rgb{0, 255, 0}));

    s.alert = true;
    Outputs alert = computeOutputs(s, now);
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, now, BUTTON_PHASE_IDLE, 0, alert.led) == alert.led));
    TEST_ASSERT_TRUE((alert.led == Rgb{255, 0, 0}));

    s.ruleLight.set(now, 5000);
    s.ruleLightColor = Rgb{0, 0, 255};
    Outputs rule = computeOutputs(s, now);
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, now, BUTTON_PHASE_IDLE, 0, rule.led) == Rgb{0, 0, 255}));

    s.manualLight.set(now, 5000);
    s.manualLightColor = Rgb{255, 128, 0};
    Outputs manual = computeOutputs(s, now);
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, now, BUTTON_PHASE_IDLE, 0, manual.led) == Rgb{255, 128, 0}));
}

void test_fatal_red_blink(void) {
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_FATAL, 0) == Rgb{255, 0, 0}));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_FATAL, 300) == kBlack));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_FATAL, 600) == Rgb{255, 0, 0}));
}

// ---- Button overlay ----------------------------------------------------------

static int transitions(uint32_t heldMs) {
    int count = 0;
    Rgb prev = colorFor(LED_SYSTEM_CONNECTED, 0, BUTTON_PHASE_COUNTDOWN, heldMs);
    for (uint32_t t = 1; t <= 600; t++) {
        Rgb c = colorFor(LED_SYSTEM_CONNECTED, t, BUTTON_PHASE_COUNTDOWN, heldMs);
        if (c != prev) count++;
        prev = c;
    }
    return count;
}

void test_button_overlay(void) {
    // PRESSED leaves the system color alone.
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTING, 0, BUTTON_PHASE_PRESSED, 100) == Rgb{0, 0, 255}));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, 0, BUTTON_PHASE_PRESSED, 100, Rgb{12, 34, 56}) == Rgb{12, 34, 56}));

    // COUNTDOWN is magenta or black only.
    for (uint32_t t = 0; t <= 2000; t += 10) {
        Rgb c = colorFor(LED_SYSTEM_CONNECTED, t, BUTTON_PHASE_COUNTDOWN, 1000);
        TEST_ASSERT_TRUE((c == Rgb{255, 0, 255}) || (c == kBlack));
    }
    TEST_ASSERT_TRUE(ledCountdownPeriodMs(2900) < ledCountdownPeriodMs(500));
    TEST_ASSERT_TRUE(transitions(2900) > transitions(500));

    // CONFIRMED is a fast white blink.
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, 0, BUTTON_PHASE_CONFIRMED, 3000) == Rgb{255, 255, 255}));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, 60, BUTTON_PHASE_CONFIRMED, 3000) == kBlack));
    TEST_ASSERT_TRUE((colorFor(LED_SYSTEM_CONNECTED, 120, BUTTON_PHASE_CONFIRMED, 3000) == Rgb{255, 255, 255}));

    // Overlays win over every system state.
    const LedSystem all[] = {LED_SYSTEM_CONNECTED, LED_SYSTEM_CONNECTING, LED_SYSTEM_PORTAL_READY,
                             LED_SYSTEM_PORTAL_CLIENT, LED_SYSTEM_PORTAL_CREDENTIALS, LED_SYSTEM_FATAL};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        TEST_ASSERT_TRUE((colorFor(all[i], 0, BUTTON_PHASE_CONFIRMED, 3000) == Rgb{255, 255, 255}));
        Rgb c = colorFor(all[i], 0, BUTTON_PHASE_COUNTDOWN, 1000);
        TEST_ASSERT_TRUE((c == Rgb{255, 0, 255}));
    }
}

// ---- Distinguishable -----------------------------------------------------------

void test_states_are_pairwise_distinguishable(void) {
    const LedSystem all[] = {LED_SYSTEM_CONNECTING, LED_SYSTEM_PORTAL_READY, LED_SYSTEM_PORTAL_CLIENT,
                             LED_SYSTEM_PORTAL_CREDENTIALS, LED_SYSTEM_FATAL, LED_SYSTEM_CONNECTED};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); j++) {
            TEST_ASSERT_TRUE(sequence(all[i]) != sequence(all[j]));
        }
    }
}

// ---- Portal selection and client counter ---------------------------------------

void test_system_for_portal_truth_table(void) {
    TEST_ASSERT_EQUAL_INT(LED_SYSTEM_PORTAL_CREDENTIALS, ledSystemForPortal(true, 0));
    TEST_ASSERT_EQUAL_INT(LED_SYSTEM_PORTAL_CREDENTIALS, ledSystemForPortal(true, 3));
    TEST_ASSERT_EQUAL_INT(LED_SYSTEM_PORTAL_CLIENT, ledSystemForPortal(false, 1));
    TEST_ASSERT_EQUAL_INT(LED_SYSTEM_PORTAL_READY, ledSystemForPortal(false, 0));
    TEST_ASSERT_EQUAL_INT(LED_SYSTEM_PORTAL_READY, ledSystemForPortal(false, -2));
}

void test_ap_clients_after(void) {
    TEST_ASSERT_EQUAL_INT(1, ledApClientsAfter(0, true));
    TEST_ASSERT_EQUAL_INT(0, ledApClientsAfter(1, false));
    TEST_ASSERT_EQUAL_INT(0, ledApClientsAfter(0, false));
    TEST_ASSERT_EQUAL_INT(1, ledApClientsAfter(-3, true));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_blink_on);
    RUN_TEST(test_pulse_level);
    RUN_TEST(test_scale);
    RUN_TEST(test_countdown_period);
    RUN_TEST(test_connecting_blue_blink);
    RUN_TEST(test_portal_ready_pulses_cyan);
    RUN_TEST(test_portal_client_solid_cyan);
    RUN_TEST(test_portal_credentials_pulses_yellow);
    RUN_TEST(test_connected_passthrough_uses_alert_state);
    RUN_TEST(test_fatal_red_blink);
    RUN_TEST(test_button_overlay);
    RUN_TEST(test_states_are_pairwise_distinguishable);
    RUN_TEST(test_system_for_portal_truth_table);
    RUN_TEST(test_ap_clients_after);
    return UNITY_END();
}

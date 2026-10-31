#include <stdint.h>
#include <unity.h>

#include "alert_state.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgb kWhite = {255, 255, 255};
static const Rgb kRed = {255, 0, 0};
static const Rgb kGreen = {0, 255, 0};
static const Rgb kBlue = {0, 0, 255};

static AlertState fresh() { return AlertState(); }

void test_idle_is_green_and_buzzer_off(void) {
    AlertState s = fresh();
    Outputs o = computeOutputs(s, 0);
    TEST_ASSERT_TRUE(o.led == kGreen);
    TEST_ASSERT_FALSE(o.buzzer);
}

void test_manual_light_survives_alert_true_then_false_until_ttl(void) {
    AlertState s = fresh();
    s.manualLight.set(1000, kManualHoldTtlMs);
    s.manualLightColor = kWhite;

    s.alert = true;
    TEST_ASSERT_TRUE(computeOutputs(s, 1500).led == kWhite);
    s.alert = false;  // what clearLocalAlert does now: only the flag
    TEST_ASSERT_TRUE(computeOutputs(s, 2000).led == kWhite);

    // Just before TTL still held, at TTL back to idle.
    TEST_ASSERT_TRUE(computeOutputs(s, 1000 + kManualHoldTtlMs - 1).led == kWhite);
    TEST_ASSERT_TRUE(computeOutputs(s, 1000 + kManualHoldTtlMs).led == kGreen);
}

void test_manual_light_explicit_off_returns_to_idle_immediately(void) {
    AlertState s = fresh();
    s.manualLight.set(1000, kManualHoldTtlMs);
    s.manualLightColor = kWhite;
    TEST_ASSERT_TRUE(computeOutputs(s, 1100).led == kWhite);
    s.manualLight.clear();
    TEST_ASSERT_TRUE(computeOutputs(s, 1200).led == kGreen);
}

void test_manual_buzzer_persists_across_alert_clear_and_stops_at_ttl(void) {
    AlertState s = fresh();
    s.manualBuzzer.set(0, kManualHoldTtlMs);
    s.alert = true;
    TEST_ASSERT_TRUE(computeOutputs(s, 100).buzzer);
    s.alert = false;
    TEST_ASSERT_TRUE(computeOutputs(s, 5000).buzzer);
    TEST_ASSERT_TRUE(computeOutputs(s, kManualHoldTtlMs - 1).buzzer);
    TEST_ASSERT_FALSE(computeOutputs(s, kManualHoldTtlMs).buzzer);
}

void test_manual_buzzer_explicit_off_is_immediate(void) {
    AlertState s = fresh();
    s.manualBuzzer.set(0, kManualHoldTtlMs);
    TEST_ASSERT_TRUE(computeOutputs(s, 10).buzzer);
    s.manualBuzzer.clear();
    TEST_ASSERT_FALSE(computeOutputs(s, 20).buzzer);
}

void test_caller_ttl_replaces_default(void) {
    AlertState s = fresh();
    s.manualLight.set(0, 5000);
    s.manualLightColor = kWhite;
    TEST_ASSERT_TRUE(computeOutputs(s, 4999).led == kWhite);
    TEST_ASSERT_TRUE(computeOutputs(s, 5000).led == kGreen);
}

void test_led_priority_manual_beats_rule_beats_alert_beats_idle(void) {
    AlertState s = fresh();
    s.alert = true;
    TEST_ASSERT_TRUE(computeOutputs(s, 0).led == kRed);  // alert over idle

    s.ruleLight.set(0, kRuleEffectMs);
    s.ruleLightColor = kBlue;
    TEST_ASSERT_TRUE(computeOutputs(s, 100).led == kBlue);  // rule over alert

    s.manualLight.set(0, kManualHoldTtlMs);
    s.manualLightColor = kWhite;
    TEST_ASSERT_TRUE(computeOutputs(s, 200).led == kWhite);  // manual over rule

    s.manualLight.clear();
    TEST_ASSERT_TRUE(computeOutputs(s, 300).led == kBlue);  // rule shows again
    TEST_ASSERT_TRUE(computeOutputs(s, kRuleEffectMs).led == kRed);  // rule ended, alert shows
    s.alert = false;
    TEST_ASSERT_TRUE(computeOutputs(s, kRuleEffectMs).led == kGreen);  // idle
}

void test_rule_light_lasts_exactly_its_duration(void) {
    AlertState s = fresh();
    s.ruleLight.set(1000, kRuleEffectMs);
    s.ruleLightColor = kBlue;
    TEST_ASSERT_TRUE(computeOutputs(s, 1000 + kRuleEffectMs - 1).led == kBlue);
    TEST_ASSERT_TRUE(computeOutputs(s, 1000 + kRuleEffectMs).led == kGreen);
    s.alert = true;
    TEST_ASSERT_TRUE(computeOutputs(s, 1000 + kRuleEffectMs).led == kRed);
}

void test_buzzer_on_for_manual_or_rule_regardless_of_alert(void) {
    AlertState s = fresh();
    s.alert = true;
    TEST_ASSERT_FALSE(computeOutputs(s, 0).buzzer);  // alert flag alone: pulse is blocking, not held
    s.ruleBuzzer.set(0, kRuleEffectMs);
    TEST_ASSERT_TRUE(computeOutputs(s, 100).buzzer);
    TEST_ASSERT_FALSE(computeOutputs(s, kRuleEffectMs).buzzer);
    s.alert = false;
    s.manualBuzzer.set(0, kManualHoldTtlMs);
    TEST_ASSERT_TRUE(computeOutputs(s, kRuleEffectMs).buzzer);
}

void test_color_from_name_named_case_insensitive(void) {
    TEST_ASSERT_TRUE(colorFromName("red", kWhite) == kRed);
    TEST_ASSERT_TRUE(colorFromName("RED", kWhite) == kRed);
    Rgb yellow = {255, 255, 0};
    TEST_ASSERT_TRUE(colorFromName("Yellow", kRed) == yellow);
    Rgb orange = {255, 128, 0};
    TEST_ASSERT_TRUE(colorFromName("orange", kRed) == orange);
    Rgb purple = {128, 0, 255};
    TEST_ASSERT_TRUE(colorFromName("purple", kRed) == purple);
    Rgb cyan = {0, 255, 255};
    TEST_ASSERT_TRUE(colorFromName("cyan", kRed) == cyan);
    Rgb magenta = {255, 0, 255};
    TEST_ASSERT_TRUE(colorFromName("magenta", kRed) == magenta);
    TEST_ASSERT_TRUE(colorFromName("green", kRed) == kGreen);
    TEST_ASSERT_TRUE(colorFromName("blue", kRed) == kBlue);
    TEST_ASSERT_TRUE(colorFromName("white", kRed) == kWhite);
}

void test_color_from_name_hex(void) {
    Rgb expected = {0, 255, 127};
    TEST_ASSERT_TRUE(colorFromName("#00ff7f", kRed) == expected);
    Rgb upper = {171, 205, 239};
    TEST_ASSERT_TRUE(colorFromName("#ABCDEF", kRed) == upper);
}

void test_color_from_name_fallback_for_bad_input(void) {
    TEST_ASSERT_TRUE(colorFromName(nullptr, kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("#12345", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("#1234567", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("#gg0000", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("chartreuse", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("redd", kWhite) == kWhite);
    TEST_ASSERT_TRUE(colorFromName("re", kWhite) == kWhite);
}

void test_color_from_name_overlong_input_is_safe(void) {
    char longName[300];
    for (int i = 0; i < 299; i++) longName[i] = 'r';
    longName[299] = '\0';
    TEST_ASSERT_TRUE(colorFromName(longName, kWhite) == kWhite);
}

void test_hold_is_wrap_safe(void) {
    Hold h;
    uint32_t start = 0xFFFFFFFFu - 99u;  // 100 ms before the counter wraps
    h.set(start, kManualHoldTtlMs);
    uint32_t later = start + 1000u;      // wraps past zero
    TEST_ASSERT_TRUE(later < start);
    TEST_ASSERT_TRUE(h.isActive(later));
    TEST_ASSERT_FALSE(h.isActive(start + kManualHoldTtlMs));
}

void test_clamp_duration(void) {
    TEST_ASSERT_EQUAL_UINT32(0, clampDurationMs(0));
    TEST_ASSERT_EQUAL_UINT32(0, clampDurationMs(-5));
    TEST_ASSERT_EQUAL_UINT32(1000, clampDurationMs(1));
    TEST_ASSERT_EQUAL_UINT32(5000, clampDurationMs(5));
    TEST_ASSERT_EQUAL_UINT32(300000, clampDurationMs(300));
    TEST_ASSERT_EQUAL_UINT32(300000, clampDurationMs(9999));
    TEST_ASSERT_EQUAL_UINT32(1000, clampDurationMs(0.4));  // positive but sub-second clamps up to 1 s
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_idle_is_green_and_buzzer_off);
    RUN_TEST(test_manual_light_survives_alert_true_then_false_until_ttl);
    RUN_TEST(test_manual_light_explicit_off_returns_to_idle_immediately);
    RUN_TEST(test_manual_buzzer_persists_across_alert_clear_and_stops_at_ttl);
    RUN_TEST(test_manual_buzzer_explicit_off_is_immediate);
    RUN_TEST(test_caller_ttl_replaces_default);
    RUN_TEST(test_led_priority_manual_beats_rule_beats_alert_beats_idle);
    RUN_TEST(test_rule_light_lasts_exactly_its_duration);
    RUN_TEST(test_buzzer_on_for_manual_or_rule_regardless_of_alert);
    RUN_TEST(test_color_from_name_named_case_insensitive);
    RUN_TEST(test_color_from_name_hex);
    RUN_TEST(test_color_from_name_fallback_for_bad_input);
    RUN_TEST(test_color_from_name_overlong_input_is_safe);
    RUN_TEST(test_hold_is_wrap_safe);
    RUN_TEST(test_clamp_duration);
    return UNITY_END();
}

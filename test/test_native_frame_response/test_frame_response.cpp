#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <unity.h>

#include "frame_response.h"

void setUp(void) {}
void tearDown(void) {}

// Parses `json` and returns the document so tests can read keys from it.
static void load(JsonDocument &doc, const char *json) {
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE_MESSAGE(err, "test JSON must parse");
}

void test_rule_command_light_with_color(void) {
    JsonDocument doc;
    load(doc, "{\"rule_command\":{\"actuator\":\"light\",\"action\":{\"color\":\"red\"},\"rule\":\"x\"}}");
    RuleCommand rc = parseRuleCommand(doc["rule_command"]);
    TEST_ASSERT_TRUE(rc.present);
    TEST_ASSERT_TRUE(rc.actuator == ActuatorKind::Light);
    TEST_ASSERT_TRUE(rc.on);
    TEST_ASSERT_EQUAL_STRING("red", rc.color);
}

void test_rule_command_buzzer_empty_action(void) {
    JsonDocument doc;
    load(doc, "{\"rule_command\":{\"actuator\":\"buzzer\",\"action\":{},\"rule\":\"x\"}}");
    RuleCommand rc = parseRuleCommand(doc["rule_command"]);
    TEST_ASSERT_TRUE(rc.present);
    TEST_ASSERT_TRUE(rc.actuator == ActuatorKind::Buzzer);
    TEST_ASSERT_TRUE(rc.on);
    TEST_ASSERT_EQUAL_STRING("", rc.color);
}

void test_rule_command_on_false_clears(void) {
    JsonDocument doc;
    load(doc, "{\"rule_command\":{\"actuator\":\"light\",\"action\":{\"on\":false}}}");
    RuleCommand rc = parseRuleCommand(doc["rule_command"]);
    TEST_ASSERT_TRUE(rc.present);
    TEST_ASSERT_FALSE(rc.on);
}

void test_rule_command_not_present_cases(void) {
    JsonDocument doc;
    load(doc, "{\"a\":null,\"b\":5,\"c\":\"light\",\"d\":{\"actuator\":\"alert\",\"action\":{}},"
              "\"e\":{\"action\":{}},\"f\":{\"actuator\":7}}");
    TEST_ASSERT_FALSE(parseRuleCommand(doc["a"]).present);        // null
    TEST_ASSERT_FALSE(parseRuleCommand(doc["missing"]).present);  // missing
    TEST_ASSERT_FALSE(parseRuleCommand(doc["b"]).present);        // non-object
    TEST_ASSERT_FALSE(parseRuleCommand(doc["c"]).present);        // string, not object
    TEST_ASSERT_FALSE(parseRuleCommand(doc["d"]).present);        // unknown actuator
    TEST_ASSERT_FALSE(parseRuleCommand(doc["e"]).present);        // no actuator
    TEST_ASSERT_FALSE(parseRuleCommand(doc["f"]).present);        // non-string actuator
}

void test_rule_command_overlong_or_non_string_color_is_ignored(void) {
    JsonDocument doc;
    load(doc, "{\"long\":{\"actuator\":\"light\",\"action\":{\"color\":"
              "\"averyveryverylongcolorname_averyveryverylongcolorname\"}},"
              "\"num\":{\"actuator\":\"light\",\"action\":{\"color\":42}},"
              "\"edge\":{\"actuator\":\"light\",\"action\":{\"color\":\"0123456789abcdef\"}},"
              "\"over\":{\"actuator\":\"light\",\"action\":{\"color\":\"0123456789abcdefg\"}}}");
    RuleCommand a = parseRuleCommand(doc["long"]);
    TEST_ASSERT_TRUE(a.present);
    TEST_ASSERT_EQUAL_STRING("", a.color);
    RuleCommand b = parseRuleCommand(doc["num"]);
    TEST_ASSERT_TRUE(b.present);
    TEST_ASSERT_EQUAL_STRING("", b.color);
    RuleCommand c = parseRuleCommand(doc["edge"]);  // exactly 16 chars fits
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", c.color);
    RuleCommand d = parseRuleCommand(doc["over"]);  // 17 chars is dropped
    TEST_ASSERT_EQUAL_STRING("", d.color);
}

void test_manual_light_with_color_and_duration(void) {
    JsonDocument doc;
    load(doc, "{\"m\":{\"actuator\":\"light\",\"action\":{\"on\":true,\"color\":\"red\",\"duration_s\":5}}}");
    ManualCommand mc = parseManualCommand(doc["m"]);
    TEST_ASSERT_TRUE(mc.present);
    TEST_ASSERT_TRUE(mc.actuator == ActuatorKind::Light);
    TEST_ASSERT_TRUE(mc.on);
    TEST_ASSERT_EQUAL_STRING("red", mc.color);
    TEST_ASSERT_EQUAL_UINT32(5000, mc.ttlMs);
}

void test_manual_duration_clamped_and_missing(void) {
    JsonDocument doc;
    load(doc, "{\"big\":{\"actuator\":\"light\",\"action\":{\"on\":true,\"duration_s\":9999}},"
              "\"none\":{\"actuator\":\"light\",\"action\":{\"on\":true}},"
              "\"neg\":{\"actuator\":\"buzzer\",\"action\":{\"on\":true,\"duration_s\":-3}},"
              "\"str\":{\"actuator\":\"buzzer\",\"action\":{\"on\":true,\"duration_s\":\"5\"}}}");
    TEST_ASSERT_EQUAL_UINT32(300000, parseManualCommand(doc["big"]).ttlMs);
    TEST_ASSERT_EQUAL_UINT32(0, parseManualCommand(doc["none"]).ttlMs);
    TEST_ASSERT_EQUAL_UINT32(0, parseManualCommand(doc["neg"]).ttlMs);
    TEST_ASSERT_EQUAL_UINT32(0, parseManualCommand(doc["str"]).ttlMs);  // non-numeric ignored
}

void test_manual_buzzer_same_without_color(void) {
    JsonDocument doc;
    load(doc, "{\"m\":{\"actuator\":\"buzzer\",\"action\":{\"on\":true,\"color\":\"red\",\"duration_s\":10}}}");
    ManualCommand mc = parseManualCommand(doc["m"]);
    TEST_ASSERT_TRUE(mc.actuator == ActuatorKind::Buzzer);
    TEST_ASSERT_TRUE(mc.on);
    TEST_ASSERT_EQUAL_STRING("", mc.color);  // buzzer has no color
    TEST_ASSERT_EQUAL_UINT32(10000, mc.ttlMs);
}

void test_manual_on_defaults_to_false(void) {
    JsonDocument doc;
    load(doc, "{\"l\":{\"actuator\":\"light\",\"action\":{}},\"b\":{\"actuator\":\"buzzer\",\"action\":{}},"
              "\"s\":{\"actuator\":\"light\",\"action\":{\"on\":\"true\"}}}");
    TEST_ASSERT_FALSE(parseManualCommand(doc["l"]).on);
    TEST_ASSERT_FALSE(parseManualCommand(doc["b"]).on);
    TEST_ASSERT_FALSE(parseManualCommand(doc["s"]).on);  // non-bool treated as missing
}

void test_manual_door_lock(void) {
    JsonDocument doc;
    load(doc, "{\"m\":{\"actuator\":\"door_lock\",\"action\":{}}}");
    ManualCommand mc = parseManualCommand(doc["m"]);
    TEST_ASSERT_TRUE(mc.present);
    TEST_ASSERT_TRUE(mc.actuator == ActuatorKind::DoorLock);
}

void test_manual_pan_tilt_values_and_nan(void) {
    JsonDocument doc;
    load(doc, "{\"ok\":{\"actuator\":\"pan_tilt\",\"action\":{\"pan_delta\":2.5,\"tilt_delta\":-1}},"
              "\"miss\":{\"actuator\":\"pan_tilt\",\"action\":{\"pan_delta\":2.5}}}");
    ManualCommand ok = parseManualCommand(doc["ok"]);
    TEST_ASSERT_TRUE(ok.actuator == ActuatorKind::PanTilt);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, ok.panDelta);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, ok.tiltDelta);
    ManualCommand miss = parseManualCommand(doc["miss"]);
    TEST_ASSERT_FALSE(isnan(miss.panDelta));
    TEST_ASSERT_TRUE(isnan(miss.tiltDelta));
}

void test_manual_unknown_and_null(void) {
    JsonDocument doc;
    load(doc, "{\"u\":{\"actuator\":\"laser\",\"action\":{}},\"n\":null}");
    ManualCommand u = parseManualCommand(doc["u"]);
    TEST_ASSERT_TRUE(u.present);
    TEST_ASSERT_TRUE(u.actuator == ActuatorKind::Unknown);
    TEST_ASSERT_FALSE(parseManualCommand(doc["n"]).present);
    TEST_ASSERT_FALSE(parseManualCommand(doc["missing"]).present);
}

void test_faces_counts_known_and_unknown(void) {
    JsonDocument doc;
    load(doc, "{\"faces\":[{\"bbox\":[1,2,3,4],\"identity\":\"Ana\",\"similarity\":0.9,\"known\":true},"
              "{\"bbox\":[5,6,7,8],\"identity\":null}]}");
    FacesSummary f = parseFaces(doc["faces"]);
    TEST_ASSERT_TRUE(f.present);
    TEST_ASSERT_EQUAL_INT(2, f.count);
    TEST_ASSERT_EQUAL_INT(1, f.known);
}

void test_faces_absent_or_non_array(void) {
    JsonDocument doc;
    load(doc, "{\"x\":\"nope\",\"y\":{}}");
    FacesSummary a = parseFaces(doc["missing"]);
    TEST_ASSERT_FALSE(a.present);
    TEST_ASSERT_EQUAL_INT(0, a.count);
    TEST_ASSERT_FALSE(parseFaces(doc["x"]).present);
    TEST_ASSERT_FALSE(parseFaces(doc["y"]).present);
}

void test_faces_empty_array_is_present_with_zero(void) {
    JsonDocument doc;
    load(doc, "{\"faces\":[]}");
    FacesSummary f = parseFaces(doc["faces"]);
    TEST_ASSERT_TRUE(f.present);
    TEST_ASSERT_EQUAL_INT(0, f.count);
    TEST_ASSERT_EQUAL_INT(0, f.known);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_rule_command_light_with_color);
    RUN_TEST(test_rule_command_buzzer_empty_action);
    RUN_TEST(test_rule_command_on_false_clears);
    RUN_TEST(test_rule_command_not_present_cases);
    RUN_TEST(test_rule_command_overlong_or_non_string_color_is_ignored);
    RUN_TEST(test_manual_light_with_color_and_duration);
    RUN_TEST(test_manual_duration_clamped_and_missing);
    RUN_TEST(test_manual_buzzer_same_without_color);
    RUN_TEST(test_manual_on_defaults_to_false);
    RUN_TEST(test_manual_door_lock);
    RUN_TEST(test_manual_pan_tilt_values_and_nan);
    RUN_TEST(test_manual_unknown_and_null);
    RUN_TEST(test_faces_counts_known_and_unknown);
    RUN_TEST(test_faces_absent_or_non_array);
    RUN_TEST(test_faces_empty_array_is_present_with_zero);
    return UNITY_END();
}

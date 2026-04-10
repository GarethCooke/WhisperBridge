#include <unity.h>
#include "state.h"

void setUp()    {}
void tearDown() {}

void test_on_payload_triggers_boost() {
    TEST_ASSERT_TRUE(parseBoostCommand("ON"));
}

void test_numeric_one_triggers_boost() {
    TEST_ASSERT_TRUE(parseBoostCommand("1"));
}

void test_off_payload_does_not_trigger() {
    TEST_ASSERT_FALSE(parseBoostCommand("OFF"));
}

void test_empty_payload_does_not_trigger() {
    TEST_ASSERT_FALSE(parseBoostCommand(""));
}

void test_lowercase_on_does_not_trigger() {
    // Only exact MQTT_PAYLOAD_ON ("ON") is accepted, not "on"
    TEST_ASSERT_FALSE(parseBoostCommand("on"));
}

void test_zero_does_not_trigger() {
    TEST_ASSERT_FALSE(parseBoostCommand("0"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_on_payload_triggers_boost);
    RUN_TEST(test_numeric_one_triggers_boost);
    RUN_TEST(test_off_payload_does_not_trigger);
    RUN_TEST(test_empty_payload_does_not_trigger);
    RUN_TEST(test_lowercase_on_does_not_trigger);
    RUN_TEST(test_zero_does_not_trigger);
    return UNITY_END();
}

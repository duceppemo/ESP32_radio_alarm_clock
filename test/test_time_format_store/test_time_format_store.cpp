#include <unity.h>

#include "Preferences.h"
#include "TimeFormatStore.h"

void setUp() { Preferences::resetAll(); }
void tearDown() {}

void test_defaults_to_24_hour() {
  TimeFormatStore fmt;
  fmt.begin();

  TEST_ASSERT_TRUE(fmt.is24Hour());
}

void test_toggle_switches_to_12_hour_and_back() {
  TimeFormatStore fmt;
  fmt.begin();

  fmt.toggle();
  TEST_ASSERT_FALSE(fmt.is24Hour());

  fmt.toggle();
  TEST_ASSERT_TRUE(fmt.is24Hour());
}

void test_selection_persists_across_instances() {
  {
    TimeFormatStore fmt;
    fmt.begin();
    fmt.toggle();  // -> 12-hour
  }

  TimeFormatStore reloaded;
  reloaded.begin();

  TEST_ASSERT_FALSE(reloaded.is24Hour());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_24_hour);
  RUN_TEST(test_toggle_switches_to_12_hour_and_back);
  RUN_TEST(test_selection_persists_across_instances);
  return UNITY_END();
}

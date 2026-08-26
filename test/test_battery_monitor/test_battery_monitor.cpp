#include <unity.h>

#include "Adafruit_MAX1704X.h"
#include "BatteryMonitor.h"
#include "Config.h"

void setUp() { Adafruit_MAX17048::resetSimulated(); }
void tearDown() {}

void test_unavailable_before_begin_returns_zeroed_readings() {
  BatteryMonitor battery;

  TEST_ASSERT_FALSE(battery.available());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, battery.voltage());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, battery.percent());
  TEST_ASSERT_FALSE(battery.isLow());
}

void test_begin_failure_leaves_it_unavailable() {
  Adafruit_MAX17048::setSimulatedBeginOk(false);
  BatteryMonitor battery;

  TEST_ASSERT_FALSE(battery.begin());
  TEST_ASSERT_FALSE(battery.available());
  // Readings stay at their safe zeroed default even if the chip would
  // otherwise report something -- available() is what callers must check.
  Adafruit_MAX17048::setSimulatedPercent(72.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, battery.percent());
}

void test_begin_success_reports_real_readings() {
  Adafruit_MAX17048::setSimulatedVoltage(3.85f);
  Adafruit_MAX17048::setSimulatedPercent(72.0f);
  BatteryMonitor battery;

  TEST_ASSERT_TRUE(battery.begin());
  TEST_ASSERT_TRUE(battery.available());
  TEST_ASSERT_EQUAL_FLOAT(3.85f, battery.voltage());
  TEST_ASSERT_EQUAL_FLOAT(72.0f, battery.percent());
}

void test_is_low_reflects_the_configured_threshold() {
  BatteryMonitor battery;
  battery.begin();

  Adafruit_MAX17048::setSimulatedPercent(BatteryConfig::LowPercentThreshold);
  TEST_ASSERT_FALSE(battery.isLow());  // at the threshold, not below it

  Adafruit_MAX17048::setSimulatedPercent(BatteryConfig::LowPercentThreshold - 1);
  TEST_ASSERT_TRUE(battery.isLow());

  Adafruit_MAX17048::setSimulatedPercent(BatteryConfig::LowPercentThreshold + 1);
  TEST_ASSERT_FALSE(battery.isLow());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_unavailable_before_begin_returns_zeroed_readings);
  RUN_TEST(test_begin_failure_leaves_it_unavailable);
  RUN_TEST(test_begin_success_reports_real_readings);
  RUN_TEST(test_is_low_reflects_the_configured_threshold);
  return UNITY_END();
}

#include <unity.h>

#include "DisplayDimmer.h"

void setUp() {}
void tearDown() {}

void test_at_or_below_dim_threshold_uses_minimum_brightness() {
  TEST_ASSERT_EQUAL(20, DisplayDimmer::tftBacklightFor(0.0f));
  TEST_ASSERT_EQUAL(20, DisplayDimmer::tftBacklightFor(5.0f));
  TEST_ASSERT_EQUAL(1, DisplayDimmer::sevenSegmentBrightnessFor(5.0f));
}

void test_at_or_above_bright_threshold_uses_maximum_brightness() {
  TEST_ASSERT_EQUAL(255, DisplayDimmer::tftBacklightFor(200.0f));
  TEST_ASSERT_EQUAL(255, DisplayDimmer::tftBacklightFor(1000.0f));
  TEST_ASSERT_EQUAL(15, DisplayDimmer::sevenSegmentBrightnessFor(200.0f));
}

void test_negative_lux_clamps_to_minimum() {
  TEST_ASSERT_EQUAL(20, DisplayDimmer::tftBacklightFor(-10.0f));
  TEST_ASSERT_EQUAL(1, DisplayDimmer::sevenSegmentBrightnessFor(-10.0f));
}

void test_midpoint_lux_interpolates_halfway() {
  // Midpoint of the 5-200 lux range: (200-5)/2 + 5 = 102.5.
  TEST_ASSERT_EQUAL(138, DisplayDimmer::tftBacklightFor(102.5f));
  TEST_ASSERT_EQUAL(8, DisplayDimmer::sevenSegmentBrightnessFor(102.5f));
}

void test_brightness_is_monotonically_nondecreasing_with_lux() {
  uint8_t lastTft = 0;
  uint8_t lastSeven = 0;
  for (float lux = 0.0f; lux <= 250.0f; lux += 10.0f) {
    uint8_t tft = DisplayDimmer::tftBacklightFor(lux);
    uint8_t seven = DisplayDimmer::sevenSegmentBrightnessFor(lux);
    TEST_ASSERT_TRUE(tft >= lastTft);
    TEST_ASSERT_TRUE(seven >= lastSeven);
    lastTft = tft;
    lastSeven = seven;
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_at_or_below_dim_threshold_uses_minimum_brightness);
  RUN_TEST(test_at_or_above_bright_threshold_uses_maximum_brightness);
  RUN_TEST(test_negative_lux_clamps_to_minimum);
  RUN_TEST(test_midpoint_lux_interpolates_halfway);
  RUN_TEST(test_brightness_is_monotonically_nondecreasing_with_lux);
  return UNITY_END();
}

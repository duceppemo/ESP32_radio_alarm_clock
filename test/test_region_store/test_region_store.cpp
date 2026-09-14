#include <unity.h>

#include <cstring>

#include "Preferences.h"
#include "RegionStore.h"

void setUp() { Preferences::resetAll(); }
void tearDown() {}

void test_defaults_to_americas() {
  RegionStore region;
  region.begin();

  TEST_ASSERT_EQUAL(0, region.index());
  TEST_ASSERT_EQUAL_STRING("Americas", region.current().label);
  TEST_ASSERT_EQUAL(2, region.current().fmDeEmphasis);
  TEST_ASSERT_EQUAL(8750, region.current().fmBandStart);
  TEST_ASSERT_EQUAL(10800, region.current().fmBandEnd);
}

void test_set_index_switches_region() {
  RegionStore region;
  region.begin();

  region.setIndex(1);
  TEST_ASSERT_EQUAL_STRING("Europe / Rest of World", region.current().label);
  TEST_ASSERT_EQUAL(1, region.current().fmDeEmphasis);

  region.setIndex(2);
  TEST_ASSERT_EQUAL_STRING("Japan", region.current().label);
  // Japan's FM band is genuinely different (76-95MHz), not just a
  // different de-emphasis -- this is what RadioTuner's band clamp must
  // track rather than a fixed constant.
  TEST_ASSERT_EQUAL(7600, region.current().fmBandStart);
  TEST_ASSERT_EQUAL(9500, region.current().fmBandEnd);
}

void test_set_index_out_of_range_falls_back_to_americas() {
  RegionStore region;
  region.begin();

  region.setIndex(200);

  TEST_ASSERT_EQUAL(0, region.index());
}

void test_selection_persists_across_instances() {
  {
    RegionStore region;
    region.begin();
    region.setIndex(2);
  }

  RegionStore reloaded;
  reloaded.begin();

  TEST_ASSERT_EQUAL(2, reloaded.index());
}

void test_every_entry_has_a_label() {
  for (uint8_t i = 0; i < RegionStore::count(); i++) {
    const RegionEntry &e = RegionStore::entry(i);
    TEST_ASSERT_NOT_NULL(e.label);
    TEST_ASSERT_TRUE(strlen(e.label) > 0);
    TEST_ASSERT_TRUE(e.fmBandStart < e.fmBandEnd);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_americas);
  RUN_TEST(test_set_index_switches_region);
  RUN_TEST(test_set_index_out_of_range_falls_back_to_americas);
  RUN_TEST(test_selection_persists_across_instances);
  RUN_TEST(test_every_entry_has_a_label);
  return UNITY_END();
}

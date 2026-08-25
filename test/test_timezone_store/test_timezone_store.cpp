#include <unity.h>

#include <cstring>

#include "Preferences.h"
#include "TimezoneStore.h"

void setUp() { Preferences::resetAll(); }
void tearDown() {}

void test_defaults_to_utc() {
  TimezoneStore tz;
  tz.begin();

  TEST_ASSERT_EQUAL(0, tz.index());
  TEST_ASSERT_EQUAL_STRING("UTC", tz.label());
  TEST_ASSERT_EQUAL_STRING("UTC0", tz.posixString());
}

void test_next_and_previous_move_by_one() {
  TimezoneStore tz;
  tz.begin();

  tz.next();
  TEST_ASSERT_EQUAL(1, tz.index());

  tz.next();
  TEST_ASSERT_EQUAL(2, tz.index());

  tz.previous();
  TEST_ASSERT_EQUAL(1, tz.index());
}

void test_next_wraps_past_the_end() {
  TimezoneStore tz;
  tz.begin();

  tz.setIndex(TimezoneStore::count() - 1);
  tz.next();

  TEST_ASSERT_EQUAL(0, tz.index());
}

void test_previous_wraps_before_the_start() {
  TimezoneStore tz;
  tz.begin();

  tz.previous();  // from 0

  TEST_ASSERT_EQUAL(TimezoneStore::count() - 1, tz.index());
}

void test_set_index_out_of_range_falls_back_to_utc() {
  TimezoneStore tz;
  tz.begin();

  tz.setIndex(200);  // nowhere near a valid index

  TEST_ASSERT_EQUAL(0, tz.index());
}

void test_selection_persists_across_instances() {
  {
    TimezoneStore tz;
    tz.begin();
    tz.setIndex(3);
  }

  TimezoneStore reloaded;
  reloaded.begin();

  TEST_ASSERT_EQUAL(3, reloaded.index());
}

void test_every_entry_has_a_label_and_a_posix_string() {
  for (uint8_t i = 0; i < TimezoneStore::count(); i++) {
    const TimezoneEntry &e = TimezoneStore::entry(i);
    TEST_ASSERT_NOT_NULL(e.label);
    TEST_ASSERT_NOT_NULL(e.posix);
    TEST_ASSERT_TRUE(strlen(e.label) > 0);
    TEST_ASSERT_TRUE(strlen(e.posix) > 0);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_utc);
  RUN_TEST(test_next_and_previous_move_by_one);
  RUN_TEST(test_next_wraps_past_the_end);
  RUN_TEST(test_previous_wraps_before_the_start);
  RUN_TEST(test_set_index_out_of_range_falls_back_to_utc);
  RUN_TEST(test_selection_persists_across_instances);
  RUN_TEST(test_every_entry_has_a_label_and_a_posix_string);
  return UNITY_END();
}

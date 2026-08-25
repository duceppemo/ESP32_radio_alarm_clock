#include <unity.h>

#include "AlarmClock.h"
#include "Preferences.h"

void setUp() { Preferences::resetAll(); }
void tearDown() {}

namespace {
constexpr uint8_t kWeekdays = 0b0111110;  // Mon-Fri
constexpr uint8_t kEveryday = 0b1111111;

// 2026-08-25 is a Tuesday.
DateTime tuesdayAt(uint8_t hour, uint8_t minute, uint8_t second = 0) {
  return DateTime(2026, 8, 25, hour, minute, second);
}
// 2026-08-30 is a Sunday.
DateTime sundayAt(uint8_t hour, uint8_t minute, uint8_t second = 0) {
  return DateTime(2026, 8, 30, hour, minute, second);
}
}  // namespace

void test_starts_idle_with_no_alarms_enabled() {
  AlarmClock clock;
  clock.begin();

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
  clock.update(tuesdayAt(7, 30));
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
}

void test_triggers_on_matching_time_and_day() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kWeekdays;
  clock.setAlarm(0, a);

  clock.update(tuesdayAt(7, 30));

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));
  TEST_ASSERT_EQUAL(0, clock.ringingAlarmIndex());
}

void test_does_not_trigger_on_a_day_outside_its_mask() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kWeekdays;  // Sunday not included
  clock.setAlarm(0, a);

  clock.update(sundayAt(7, 30));

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
}

void test_does_not_trigger_when_disabled() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = false;
  a.daysMask = kEveryday;
  clock.setAlarm(0, a);

  clock.update(tuesdayAt(7, 30));

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
}

void test_does_not_retrigger_within_the_same_minute() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kEveryday;
  clock.setAlarm(0, a);

  clock.update(tuesdayAt(7, 30, 0));
  clock.dismiss();
  // Still within the same minute (7:30:45) -- must not fire again.
  clock.update(tuesdayAt(7, 30, 45));

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
}

void test_snooze_holds_then_re_rings_after_snooze_duration() {
  AlarmClock clock;
  clock.begin();
  clock.setSnoozeMinutes(9);

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kEveryday;
  clock.setAlarm(0, a);

  DateTime ringTime = tuesdayAt(7, 30);
  clock.update(ringTime);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));

  clock.snooze(ringTime);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Snoozed), static_cast<int>(clock.state()));

  // Not yet due: 8 minutes later, snooze is 9 minutes.
  clock.update(ringTime + TimeSpan(0, 0, 8, 0));
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Snoozed), static_cast<int>(clock.state()));

  // Due: 9 minutes later.
  clock.update(ringTime + TimeSpan(0, 0, 9, 0));
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));
}

void test_dismiss_clears_ringing_state() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kEveryday;
  clock.setAlarm(0, a);

  clock.update(tuesdayAt(7, 30));
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));

  clock.dismiss();
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(clock.state()));
  TEST_ASSERT_EQUAL(-1, clock.ringingAlarmIndex());
}

void test_earliest_matching_alarm_wins_when_two_share_a_time() {
  AlarmClock clock;
  clock.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 30;
  a.enabled = true;
  a.daysMask = kEveryday;
  clock.setAlarm(0, a);
  clock.setAlarm(1, a);

  clock.update(tuesdayAt(7, 30));

  TEST_ASSERT_EQUAL(0, clock.ringingAlarmIndex());
}

void test_settings_persist_across_instances_via_preferences() {
  {
    AlarmClock clock;
    clock.begin();

    Alarm a;
    a.hour = 6;
    a.minute = 15;
    a.enabled = true;
    a.daysMask = kWeekdays;
    clock.setAlarm(1, a);
    clock.setSnoozeMinutes(5);
  }

  // A fresh instance should load what the previous one saved -- Preferences
  // is NOT reset here, unlike setUp(), to simulate a reboot.
  AlarmClock reloaded;
  reloaded.begin();

  TEST_ASSERT_EQUAL(6, reloaded.alarm(1).hour);
  TEST_ASSERT_EQUAL(15, reloaded.alarm(1).minute);
  TEST_ASSERT_TRUE(reloaded.alarm(1).enabled);
  TEST_ASSERT_EQUAL(5, reloaded.snoozeMinutes());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle_with_no_alarms_enabled);
  RUN_TEST(test_triggers_on_matching_time_and_day);
  RUN_TEST(test_does_not_trigger_on_a_day_outside_its_mask);
  RUN_TEST(test_does_not_trigger_when_disabled);
  RUN_TEST(test_does_not_retrigger_within_the_same_minute);
  RUN_TEST(test_snooze_holds_then_re_rings_after_snooze_duration);
  RUN_TEST(test_dismiss_clears_ringing_state);
  RUN_TEST(test_earliest_matching_alarm_wins_when_two_share_a_time);
  RUN_TEST(test_settings_persist_across_instances_via_preferences);
  return UNITY_END();
}

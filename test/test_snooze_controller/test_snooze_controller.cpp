#include <unity.h>

#include "AlarmClock.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "SI4735.h"
#include "SnoozeController.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  native_fake_millis_value() = 0;
}
void tearDown() {}

namespace {
const DateTime kNow(2026, 8, 25, 7, 0, 0);
}  // namespace

void test_press_while_ringing_snoozes_the_alarm() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  SnoozeController snooze(alarms, radio);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  alarms.setAlarm(0, a);
  alarms.update(kNow);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(alarms.state()));

  snooze.onSnoozePressed(kNow);

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Snoozed), static_cast<int>(alarms.state()));
}

void test_press_while_ringing_does_not_touch_the_radio() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  radio.setMuted(false);
  SnoozeController snooze(alarms, radio);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  alarms.setAlarm(0, a);
  alarms.update(kNow);

  snooze.onSnoozePressed(kNow);

  // A ringing alarm takes priority -- the radio-as-sleep-timer path must
  // not also fire.
  TEST_ASSERT_FALSE(radio.sleepTimerActive());
}

void test_press_while_idle_and_radio_on_starts_sleep_timer() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  radio.setMuted(false);
  SnoozeController snooze(alarms, radio);

  snooze.onSnoozePressed(kNow);

  TEST_ASSERT_TRUE(radio.sleepTimerActive());
  TEST_ASSERT_EQUAL(RadioConfig::DefaultSleepTimerMinutes, radio.sleepTimerRemainingMinutes());
}

void test_second_press_while_sleep_timer_active_cancels_it() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  radio.setMuted(false);
  SnoozeController snooze(alarms, radio);

  snooze.onSnoozePressed(kNow);
  TEST_ASSERT_TRUE(radio.sleepTimerActive());

  snooze.onSnoozePressed(kNow);
  TEST_ASSERT_FALSE(radio.sleepTimerActive());
}

void test_press_while_idle_and_radio_muted_does_nothing() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  radio.setMuted(true);
  SnoozeController snooze(alarms, radio);

  snooze.onSnoozePressed(kNow);

  TEST_ASSERT_FALSE(radio.sleepTimerActive());
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(alarms.state()));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_press_while_ringing_snoozes_the_alarm);
  RUN_TEST(test_press_while_ringing_does_not_touch_the_radio);
  RUN_TEST(test_press_while_idle_and_radio_on_starts_sleep_timer);
  RUN_TEST(test_second_press_while_sleep_timer_active_cancels_it);
  RUN_TEST(test_press_while_idle_and_radio_muted_does_nothing);
  return UNITY_END();
}

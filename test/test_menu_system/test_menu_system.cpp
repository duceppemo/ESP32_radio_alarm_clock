#include <unity.h>

#include "AlarmClock.h"
#include "Adafruit_ST7789.h"
#include "MenuSystem.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "SI4735.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  native_fake_millis_value() = 1000;  // start away from 0 so debounce math is unambiguous
}
void tearDown() {}

namespace {
const DateTime kNow(2026, 8, 25, 7, 0, 0);

void advance(uint32_t deltaMs) { native_fake_millis_value() += deltaMs; }

// Short press: pressed for less than MenuSystem's 600ms long-press threshold.
void tap(uint8_t pin, MenuSystem &menu) {
  native_fake_digital_state(pin) = LOW;
  advance(50);
  menu.update(kNow, "");
  native_fake_digital_state(pin) = HIGH;
  advance(50);
  menu.update(kNow, "");
}

// Long press: held past the 600ms threshold before releasing.
void hold(uint8_t pin, MenuSystem &menu) {
  native_fake_digital_state(pin) = LOW;
  advance(50);
  menu.update(kNow, "");
  advance(700);
  native_fake_digital_state(pin) = HIGH;
  advance(50);
  menu.update(kNow, "");
}
}  // namespace

void test_toggling_an_alarm_enabled_through_the_full_edit_flow() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  TEST_ASSERT_FALSE(alarms.alarm(0).enabled);

  tap(Pins::MenuSelect, menu);  // Home (cursor 0 = Alarms) -> AlarmList
  tap(Pins::MenuSelect, menu);  // AlarmList (alarm 0) -> AlarmEdit, field 0 = Enabled
  tap(Pins::MenuUp, menu);      // toggle Enabled: false -> true

  // Walk fields 0->5 (Enabled/Hour/Minute/Days/Wake/Save): 5 taps to reach
  // Save, a 6th to commit it.
  for (int i = 0; i < 6; i++) tap(Pins::MenuSelect, menu);

  TEST_ASSERT_TRUE(alarms.alarm(0).enabled);
}

void test_editing_hour_and_minute_then_saving() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  tap(Pins::MenuSelect, menu);  // Home -> AlarmList
  tap(Pins::MenuSelect, menu);  // AlarmList -> AlarmEdit, field 0 = Enabled

  tap(Pins::MenuSelect, menu);  // advance to field 1 = Hour
  tap(Pins::MenuUp, menu);      // default hour 7 -> 8
  tap(Pins::MenuUp, menu);      // 8 -> 9

  tap(Pins::MenuSelect, menu);  // advance to field 2 = Minute
  tap(Pins::MenuUp, menu);      // default minute 0 -> 1

  // From field 2: 3 taps to reach Save (field 5), a 4th to commit it.
  for (int i = 0; i < 4; i++) tap(Pins::MenuSelect, menu);

  TEST_ASSERT_EQUAL(9, alarms.alarm(0).hour);
  TEST_ASSERT_EQUAL(1, alarms.alarm(0).minute);
}

void test_cancelling_an_edit_with_long_press_discards_changes() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  tap(Pins::MenuSelect, menu);   // Home -> AlarmList
  tap(Pins::MenuSelect, menu);   // AlarmList -> AlarmEdit
  tap(Pins::MenuUp, menu);       // toggle Enabled (in the working copy only)
  hold(Pins::MenuSelect, menu);  // long press -> discard, back to AlarmList

  TEST_ASSERT_FALSE(alarms.alarm(0).enabled);  // never saved
}

void test_radio_screen_tune_up_and_mute() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen
  tap(Pins::MenuUp, menu);      // tune up by one step

  TEST_ASSERT_EQUAL(startFreq + RadioConfig::FmStep, radio.frequency10kHz());
  TEST_ASSERT_FALSE(radio.muted());

  tap(Pins::MenuSelect, menu);  // toggle mute
  TEST_ASSERT_TRUE(radio.muted());
}

void test_ringing_alarm_short_press_snoozes() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  alarms.setAlarm(0, a);
  alarms.update(kNow);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(alarms.state()));

  tap(Pins::MenuSelect, menu);  // Home, ringing: short press = snooze

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Snoozed), static_cast<int>(alarms.state()));
}

void test_ringing_alarm_long_press_dismisses() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  MenuSystem menu(tft, alarms, radio, nullptr);
  menu.begin();

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  alarms.setAlarm(0, a);
  alarms.update(kNow);

  hold(Pins::MenuSelect, menu);  // Home, ringing: long press = dismiss

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Idle), static_cast<int>(alarms.state()));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_toggling_an_alarm_enabled_through_the_full_edit_flow);
  RUN_TEST(test_editing_hour_and_minute_then_saving);
  RUN_TEST(test_cancelling_an_edit_with_long_press_discards_changes);
  RUN_TEST(test_radio_screen_tune_up_and_mute);
  RUN_TEST(test_ringing_alarm_short_press_snoozes);
  RUN_TEST(test_ringing_alarm_long_press_dismisses);
  return UNITY_END();
}

#include <unity.h>

#include "AlarmClock.h"
#include "Adafruit_ST7789.h"
#include "MenuSystem.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "SI4735.h"
#include "TimezoneStore.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedPresent();
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
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
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
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
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
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
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
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen
  tap(Pins::MenuUp, menu);      // tune up by one step

  TEST_ASSERT_EQUAL(startFreq + RadioConfig::FmStep, radio.frequency10kHz());
  TEST_ASSERT_FALSE(radio.muted());

  tap(Pins::MenuSelect, menu);  // toggle mute
  TEST_ASSERT_TRUE(radio.muted());
}

void test_radio_screen_does_nothing_when_no_radio_is_present() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  SI4735::setSimulatedPresent(false);
  TEST_ASSERT_FALSE(radio.begin());
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen
  tap(Pins::MenuUp, menu);      // would tune up, if a radio were present
  tap(Pins::MenuSelect, menu);  // would toggle mute, if a radio were present

  TEST_ASSERT_EQUAL(startFreq, radio.frequency10kHz());
  TEST_ASSERT_FALSE(radio.muted());

  hold(Pins::MenuSelect, menu);  // long-press back to Home still works
}

void test_ringing_alarm_short_press_snoozes() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
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
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
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

void test_set_time_saves_the_new_hour_and_minute() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuDown, menu);    // Radio(1) -> WiFi(2)
  tap(Pins::MenuDown, menu);    // WiFi(2) -> Time(3)
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Hour (starts at kNow's 7:00)

  tap(Pins::MenuUp, menu);      // hour 7 -> 8
  tap(Pins::MenuUp, menu);      // hour 8 -> 9

  tap(Pins::MenuSelect, menu);  // advance to field 1 = Minute
  tap(Pins::MenuUp, menu);      // minute 0 -> 1

  tap(Pins::MenuSelect, menu);  // advance to field 2 = Save
  tap(Pins::MenuSelect, menu);  // commit

  TEST_ASSERT_EQUAL(9, rtc.now().hour());
  TEST_ASSERT_EQUAL(1, rtc.now().minute());
  // The date must carry over from the current time, not reset.
  TEST_ASSERT_EQUAL(kNow.year(), rtc.now().year());
  TEST_ASSERT_EQUAL(kNow.month(), rtc.now().month());
  TEST_ASSERT_EQUAL(kNow.day(), rtc.now().day());
}

void test_set_time_cancelled_with_long_press_does_not_save() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time
  tap(Pins::MenuUp, menu);      // hour 7 -> 8 (working copy only)
  hold(Pins::MenuSelect, menu); // cancel

  TEST_ASSERT_EQUAL(kNow.hour(), rtc.now().hour());
}

void test_set_time_with_no_rtc_does_not_crash() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time
  tap(Pins::MenuSelect, menu);  // -> field 1
  tap(Pins::MenuSelect, menu);  // -> field 2 (Save)
  tap(Pins::MenuSelect, menu);  // commit with a null rtc_ -- must not crash

  TEST_ASSERT_TRUE(true);  // reaching here without crashing is the assertion
}

void test_set_rtc_unavailable_skips_saving_even_with_a_non_null_rtc() {
  // Regression: main.cpp constructs MenuSystem with &rtc before rtc.begin()
  // is ever called (it's a global, wired up before setup() runs), so a
  // failed rtc.begin() used to have no way to stop Set Time from silently
  // "saving" to hardware that was never actually there. setRtcAvailable(false)
  // is how setup() corrects that after the fact.
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone);
  menu.begin();
  menu.setRtcAvailable(false);

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Hour
  tap(Pins::MenuUp, menu);      // hour 7 -> 8
  tap(Pins::MenuSelect, menu);  // -> field 1
  tap(Pins::MenuSelect, menu);  // -> field 2 (Save)
  tap(Pins::MenuSelect, menu);  // commit -- must not touch the (unavailable) rtc

  TEST_ASSERT_EQUAL(kNow.hour(), rtc.now().hour());
}

void test_timezone_screen_cycles_selection() {
  AlarmClock alarms;
  alarms.begin();
  RadioTuner radio;
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone);
  menu.begin();

  TEST_ASSERT_EQUAL(0, timezone.index());  // UTC by default

  tap(Pins::MenuDown, menu);  // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuDown, menu);  // Radio(1) -> WiFi(2)
  tap(Pins::MenuDown, menu);  // WiFi(2) -> Time(3)
  tap(Pins::MenuDown, menu);  // Time(3) -> TZ(4)
  tap(Pins::MenuSelect, menu);  // enter Timezone screen

  tap(Pins::MenuUp, menu);
  tap(Pins::MenuUp, menu);
  TEST_ASSERT_EQUAL(2, timezone.index());

  tap(Pins::MenuDown, menu);
  TEST_ASSERT_EQUAL(1, timezone.index());

  hold(Pins::MenuSelect, menu);  // back to Home -- selection stays as left
  TEST_ASSERT_EQUAL(1, timezone.index());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_toggling_an_alarm_enabled_through_the_full_edit_flow);
  RUN_TEST(test_editing_hour_and_minute_then_saving);
  RUN_TEST(test_cancelling_an_edit_with_long_press_discards_changes);
  RUN_TEST(test_radio_screen_tune_up_and_mute);
  RUN_TEST(test_radio_screen_does_nothing_when_no_radio_is_present);
  RUN_TEST(test_ringing_alarm_short_press_snoozes);
  RUN_TEST(test_ringing_alarm_long_press_dismisses);
  RUN_TEST(test_set_time_saves_the_new_hour_and_minute);
  RUN_TEST(test_set_time_cancelled_with_long_press_does_not_save);
  RUN_TEST(test_set_time_with_no_rtc_does_not_crash);
  RUN_TEST(test_set_rtc_unavailable_skips_saving_even_with_a_non_null_rtc);
  RUN_TEST(test_timezone_screen_cycles_selection);
  return UNITY_END();
}

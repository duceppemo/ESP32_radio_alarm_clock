#include <unity.h>

#include "AlarmClock.h"
#include "Adafruit_ST7789.h"
#include "MenuSystem.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "RegionStore.h"
#include "SI4735.h"
#include "TimeFormatStore.h"
#include "TimezoneStore.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedSnr();
  SI4735::resetSimulatedPresent();
  SI4735::clearSimulatedSignalAt();
  native_fake_millis_value() = 1000;  // start away from 0 so debounce math is unambiguous
  // native_fake_digital_state() defaults every pin to HIGH (matching the
  // active-low buttons' idle level), which is wrong for MenuUp/MenuDown --
  // this board wires those two active-high, idle LOW. Left at the fake's
  // default, a fresh DebouncedButton (stableState_ starts false/not-pressed)
  // would see the real reading disagree with that on its very first update()
  // call and register a spurious phantom press before the test ever touches
  // the pin. Set every button pin to its actual idle level explicitly.
  native_fake_digital_state(Pins::MenuSelect) = HIGH;
  native_fake_digital_state(Pins::MenuUp) = LOW;
  native_fake_digital_state(Pins::MenuDown) = LOW;
}
void tearDown() {}

namespace {
const DateTime kNow(2026, 8, 25, 7, 0, 0);

// Americas (RegionStore's default, index 0) has real-world 200kHz channel
// spacing -- see test_radio_tuner.cpp's own copy of this constant for why
// it's hardcoded rather than read back from RegionStore::entry(0).fmStep.
constexpr uint16_t kFmStep = 20;

void advance(uint32_t deltaMs) { native_fake_millis_value() += deltaMs; }

// MenuSelect (D0) is active-low (idle HIGH); MenuUp/MenuDown (D1/D2) are
// wired the opposite way on this board (idle LOW) -- see DebouncedButton's
// activeHigh. Getting this backwards leaves a pin "stuck" reading pressed
// after a tap()/hold() call returns, which a later, unrelated tap() on a
// different pin would then see as a still-held button once enough time
// (native_fake_millis_value()) has passed to cross the auto-repeat delay --
// a phantom repeat firing on whatever field happens to have focus then.
bool isActiveHighPin(uint8_t pin) { return pin == Pins::MenuUp || pin == Pins::MenuDown; }

// seekUp()/seekDown() only start a sweep -- main.cpp's loop() advances it
// via RadioTuner::update(). Same helper as test_radio_tuner's.
void runSeek(RadioTuner &radio) {
  for (int i = 0; i < 2000 && radio.seeking(); i++) {
    advance(RadioConfig::SeekSettleMs);
    radio.update();
  }
  TEST_ASSERT_FALSE_MESSAGE(radio.seeking(), "seek never finished");
}

// Short press: released well under MenuSystem's 1000ms long-press threshold.
void tap(uint8_t pin, MenuSystem &menu) {
  bool activeHigh = isActiveHighPin(pin);
  native_fake_digital_state(pin) = activeHigh ? HIGH : LOW;  // press
  advance(50);
  menu.update(kNow, "");
  native_fake_digital_state(pin) = activeHigh ? LOW : HIGH;  // release -> idle
  advance(50);
  menu.update(kNow, "");
}

// Long press: fires the instant the hold crosses the 1000ms threshold --
// while still held, not on release -- so the update() call after advancing
// past it (button still down) is what triggers it. The final release-time
// update() call should be a no-op (longPressFired_ latches until the next
// fresh press), which is exactly what callers rely on this helper for.
void hold(uint8_t pin, MenuSystem &menu) {
  bool activeHigh = isActiveHighPin(pin);
  native_fake_digital_state(pin) = activeHigh ? HIGH : LOW;  // press
  advance(50);
  menu.update(kNow, "");  // registers the press
  advance(1050);
  menu.update(kNow, "");  // still held -- long press fires here
  native_fake_digital_state(pin) = activeHigh ? LOW : HIGH;  // release -> idle
  advance(50);
  menu.update(kNow, "");  // release -- latched, must not fire anything again
}
}  // namespace

void test_toggling_an_alarm_enabled_through_the_full_edit_flow() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuSelect, menu);   // Home -> AlarmList
  tap(Pins::MenuSelect, menu);   // AlarmList -> AlarmEdit
  tap(Pins::MenuUp, menu);       // toggle Enabled (in the working copy only)
  hold(Pins::MenuSelect, menu);  // long press -> discard, back to AlarmList

  TEST_ASSERT_FALSE(alarms.alarm(0).enabled);  // never saved
}

void test_holding_through_a_long_press_screen_change_does_not_cascade_further() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuSelect, menu);  // Home -> AlarmList
  tap(Pins::MenuSelect, menu);  // AlarmList (alarm 0) -> AlarmEdit

  // Long press to cancel out of AlarmEdit (-> AlarmList), but drive it by
  // hand instead of using hold() so the button stays down well past the
  // point where the long press fires. AlarmList's own long press ALSO
  // backs out (-> Home) -- without longPressFired_ latching until a fresh
  // press, continuing to hold here would look identical to a second long
  // press on AlarmList and cascade AlarmEdit -> AlarmList -> Home in one
  // continuous hold, when only one "back" was intended.
  native_fake_digital_state(Pins::MenuSelect) = LOW;
  advance(50);
  menu.update(kNow, "");  // press registers
  advance(1050);
  menu.update(kNow, "");  // long press fires once: AlarmEdit -> AlarmList
  advance(500);
  menu.update(kNow, "");  // still held -- must not cascade to Home
  native_fake_digital_state(Pins::MenuSelect) = HIGH;
  advance(50);
  menu.update(kNow, "");  // release -- must not fire a short press either

  // If we're actually still on AlarmList (no cascade), a fresh short tap
  // re-enters AlarmEdit for alarm 0; toggle it on and save. Had a cascade
  // to Home happened instead, this same tap sequence would be read as Home
  // navigation and never reach AlarmEdit, so the alarm would never save.
  tap(Pins::MenuSelect, menu);  // AlarmList (alarm 0) -> AlarmEdit
  tap(Pins::MenuUp, menu);      // toggle Enabled: false -> true
  for (int i = 0; i < 6; i++) tap(Pins::MenuSelect, menu);  // walk to Save, commit

  TEST_ASSERT_TRUE(alarms.alarm(0).enabled);
}

void test_radio_screen_tune_up_and_mute() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen
  tap(Pins::MenuUp, menu);      // tune up by one step

  TEST_ASSERT_EQUAL(startFreq + kFmStep, radio.frequency10kHz());
  TEST_ASSERT_FALSE(radio.muted());

  tap(Pins::MenuSelect, menu);  // toggle mute
  TEST_ASSERT_TRUE(radio.muted());
}

void test_radio_screen_does_nothing_when_no_radio_is_present() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  SI4735::setSimulatedPresent(false);
  TEST_ASSERT_FALSE(radio.begin());
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen
  tap(Pins::MenuUp, menu);      // would tune up, if a radio were present
  tap(Pins::MenuSelect, menu);  // would toggle mute, if a radio were present

  TEST_ASSERT_EQUAL(startFreq, radio.frequency10kHz());
  TEST_ASSERT_FALSE(radio.muted());

  hold(Pins::MenuSelect, menu);  // long-press back to Home still works
}

void test_radio_screen_long_hold_seeks_instead_of_repeatedly_stepping() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen

  // Dead air everywhere except a station several steps above where the
  // initial tap lands -- only a genuine multi-candidate seek sweep can
  // reach it; repeated single steps (if holding still triggered those)
  // couldn't land here by coincidence.
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = startFreq + 6 * kFmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  hold(Pins::MenuUp, menu);  // press fires one immediate step, then the long hold crosses into seek
  TEST_ASSERT_TRUE(radio.seeking());  // started, not blocked-and-finished
  runSeek(radio);

  // If holding kept auto-repeating stepUp() instead of switching to a seek
  // once the hold crossed the long-press threshold, this would land 2
  // FmSteps up (one from the tap, one more from a single repeat), not on
  // the distant simulated station -- confirms a real seek swept for it.
  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_radio_screen_long_hold_fires_seek_even_off_the_repeat_schedule() {
  // Regression test for a real bug: handleInput()'s early-return guard
  // didn't include upLongHold/downLongHold, so the specific tick that
  // first crosses kLongPressMs could return before ever reaching the Radio
  // case below -- while still latching upSeekFired_/downSeekFired_ true on
  // the way out, so the seek silently never fired at all (indistinguishable
  // from a plain tap). hold()'s single big jump past the threshold happened
  // to always land on a tick where DebouncedButton's own repeat schedule
  // (every 150ms) was also due, which kept the guard from ever returning
  // early and masked the bug -- a real device's continuously-running
  // loop() has no such alignment, so this test polls finely enough (10ms)
  // to land the long-press tick off that schedule too, the way real
  // hardware actually would.
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen

  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = startFreq + 6 * kFmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  native_fake_digital_state(Pins::MenuUp) = HIGH;  // press (active-high)
  for (int i = 0; i < 120; i++) {  // 120 x 10ms = 1200ms of finely-polled holding
    advance(10);
    menu.update(kNow, "");
  }
  native_fake_digital_state(Pins::MenuUp) = LOW;  // release
  advance(50);
  menu.update(kNow, "");
  runSeek(radio);

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_alarm_ringing_on_another_screen_takes_over_home() {
  // Regression: the ALARM screen and its tap-to-snooze handling only exist
  // on Home, so an alarm firing while the user sat on the Radio screen kept
  // showing Radio -- tap toggled mute instead of snoozing.
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  alarms.setAlarm(0, a);
  alarms.update(kNow);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(alarms.state()));

  advance(50);
  menu.update(kNow, "");  // no button: the ring alone must pull the UI back to Home

  tap(Pins::MenuSelect, menu);  // now a snooze, not Radio's mute toggle
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Snoozed), static_cast<int>(alarms.state()));
  TEST_ASSERT_FALSE(radio.muted());
}

void test_radio_screen_refreshes_signal_periodically_without_a_button_press() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen -- already queries once on entry

  int callsAfterEntry = SI4735::driverCallCount();
  advance(600);  // past the 500ms signal-refresh interval -- no button touched
  menu.update(kNow, "");

  // rssi()/snr() are the only driver calls renderRadio() makes on its own,
  // so an increase here with no button press is exactly the periodic
  // refresh timer firing, not a stale cached value.
  TEST_ASSERT_TRUE(SI4735::driverCallCount() > callsAfterEntry);
}

void test_radio_screen_holding_up_under_the_long_press_threshold_does_not_repeat_step() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t startFreq = radio.frequency10kHz();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuSelect, menu);  // enter Radio screen

  // Press and hold Up for 750ms total (under the 1000ms long-press
  // threshold), ticking at the same cadence DebouncedButton's auto-repeat
  // uses (450ms initial delay, then every 150ms) -- before this fix, each
  // of these later ticks would have auto-repeated another step.
  native_fake_digital_state(Pins::MenuUp) = HIGH;  // press (active-high)
  advance(50);
  menu.update(kNow, "");  // registers the press -- one immediate step
  advance(500);
  menu.update(kNow, "");  // used to auto-repeat a step here
  advance(200);
  menu.update(kNow, "");  // and again here
  native_fake_digital_state(Pins::MenuUp) = LOW;  // release, still well under 1000ms
  advance(50);
  menu.update(kNow, "");

  TEST_ASSERT_EQUAL(startFreq + kFmStep, radio.frequency10kHz());
}

void test_ringing_alarm_short_press_snoozes() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuDown, menu);    // Radio(1) -> WiFi(2)
  tap(Pins::MenuDown, menu);    // WiFi(2) -> Time(3)
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour (starts at kNow's 7:00)

  tap(Pins::MenuUp, menu);      // hour 7 -> 8
  tap(Pins::MenuUp, menu);      // hour 8 -> 9

  tap(Pins::MenuSelect, menu);  // advance to field 4 = Minute
  tap(Pins::MenuUp, menu);      // minute 0 -> 1

  tap(Pins::MenuSelect, menu);  // advance to field 5 = Format (left untouched)
  tap(Pins::MenuSelect, menu);  // advance to field 6 = Sync Now (left untouched)
  tap(Pins::MenuSelect, menu);  // advance to field 7 = Save
  tap(Pins::MenuSelect, menu);  // commit

  TEST_ASSERT_EQUAL(9, rtc.now().hour());
  TEST_ASSERT_EQUAL(1, rtc.now().minute());
  // The date must carry over from the current time, not reset.
  TEST_ASSERT_EQUAL(kNow.year(), rtc.now().year());
  TEST_ASSERT_EQUAL(kNow.month(), rtc.now().month());
  TEST_ASSERT_EQUAL(kNow.day(), rtc.now().day());
}

void test_set_time_day_field_wraps_to_1_past_the_months_last_day() {
  // Regression: the day field used to cycle mod a fixed 31 regardless of
  // the selected month's actual length, so pressing Up on the last valid
  // day of a shorter month (e.g. Feb 28) computed an out-of-range day that
  // the automatic clamp then silently snapped straight back -- Up could
  // never actually wrap around to day 1.
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);  // August 25, 2026
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);    // Home cursor: Alarms(0) -> Radio(1)
  tap(Pins::MenuDown, menu);    // Radio(1) -> WiFi(2)
  tap(Pins::MenuDown, menu);    // WiFi(2) -> Time(3)
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month (starts at August)

  for (int i = 0; i < 6; i++) tap(Pins::MenuDown, menu);  // August -> February

  tap(Pins::MenuSelect, menu);  // -> field 2 = Day (starts at 25, unaffected -- 25 <= 28)
  for (int i = 0; i < 3; i++) tap(Pins::MenuUp, menu);  // 25 -> 26 -> 27 -> 28 (Feb's last day)
  tap(Pins::MenuUp, menu);  // one more -- should wrap to 1, not stay stuck at 28

  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour (left untouched)
  tap(Pins::MenuSelect, menu);  // -> field 4 = Minute (left untouched)
  tap(Pins::MenuSelect, menu);  // -> field 5 = Format (left untouched)
  tap(Pins::MenuSelect, menu);  // -> field 6 = Sync Now (left untouched)
  tap(Pins::MenuSelect, menu);  // -> field 7 = Save
  tap(Pins::MenuSelect, menu);  // commit

  TEST_ASSERT_EQUAL(2, rtc.now().month());
  TEST_ASSERT_EQUAL(1, rtc.now().day());
}

void test_set_time_format_field_toggles_between_24h_and_12h() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  TEST_ASSERT_TRUE(timeFormat.is24Hour());

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour
  tap(Pins::MenuSelect, menu);  // -> field 4 = Minute
  tap(Pins::MenuSelect, menu);  // -> field 5 = Format
  tap(Pins::MenuUp, menu);      // toggle 24h -> 12h

  TEST_ASSERT_FALSE(timeFormat.is24Hour());
}

// Regression: Sync Now and Save used to be two separate terminal fields in
// the same sequential walk (Sync Now before Save), so tapping through the
// screen to reach Save would fire an unwanted NTP sync -- and exit without
// saving -- the instant the cursor landed on Sync Now. Sync Now now fires
// on up/down instead of tap, so a plain tap-through always reaches Save
// untouched, and Sync Now only fires when the user deliberately presses
// up/down while sitting on that row.
void test_set_time_sync_now_requests_sync_instead_of_saving() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour
  tap(Pins::MenuUp, menu);      // hour 7 -> 8 (must NOT be saved by Sync Now)
  tap(Pins::MenuSelect, menu);  // -> field 4 = Minute
  tap(Pins::MenuSelect, menu);  // -> field 5 = Format
  tap(Pins::MenuSelect, menu);  // -> field 6 = Sync Now
  tap(Pins::MenuUp, menu);      // trigger Sync Now (online by default)

  TEST_ASSERT_TRUE(menu.consumeNtpSyncRequest());
  TEST_ASSERT_EQUAL(kNow.hour(), rtc.now().hour());  // untouched -- no rtc_->adjust() call
}

// Regression: the row must not fire (or advance the sync flag) while
// offline -- that's what "greyed out" means functionally, not just visually.
void test_set_time_sync_now_does_nothing_while_offline() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour
  tap(Pins::MenuSelect, menu);  // -> field 4 = Minute
  tap(Pins::MenuSelect, menu);  // -> field 5 = Format
  tap(Pins::MenuSelect, menu);  // -> field 6 = Sync Now

  native_fake_digital_state(Pins::MenuUp) = HIGH;  // press, offline this time
  advance(50);
  menu.update(kNow, "", /*wifiOnline=*/false);
  native_fake_digital_state(Pins::MenuUp) = LOW;
  advance(50);
  menu.update(kNow, "", /*wifiOnline=*/false);

  TEST_ASSERT_FALSE(menu.consumeNtpSyncRequest());
}

void test_set_time_cancelled_with_long_press_does_not_save() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour
  tap(Pins::MenuUp, menu);      // hour 7 -> 8 (working copy only)
  hold(Pins::MenuSelect, menu); // cancel

  TEST_ASSERT_EQUAL(kNow.hour(), rtc.now().hour());
}

void test_set_time_with_no_rtc_does_not_crash() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
  menu.begin();

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  for (int i = 0; i < 7; i++) tap(Pins::MenuSelect, menu);  // walk fields 1-7
  tap(Pins::MenuSelect, menu);  // commit (field 7 = Save) with a null rtc_ -- must not crash

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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  RTC_DS3231 rtc;
  rtc.adjust(kNow);
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, &rtc, timezone, timeFormat);
  menu.begin();
  menu.setRtcAvailable(false);

  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuDown, menu);
  tap(Pins::MenuSelect, menu);  // enter Set Time, field 0 = Year
  tap(Pins::MenuSelect, menu);  // -> field 1 = Month
  tap(Pins::MenuSelect, menu);  // -> field 2 = Day
  tap(Pins::MenuSelect, menu);  // -> field 3 = Hour
  tap(Pins::MenuUp, menu);      // hour 7 -> 8
  tap(Pins::MenuSelect, menu);  // -> field 4 = Minute
  tap(Pins::MenuSelect, menu);  // -> field 5 = Format
  tap(Pins::MenuSelect, menu);  // -> field 6 = Sync Now
  tap(Pins::MenuSelect, menu);  // -> field 7 = Save
  tap(Pins::MenuSelect, menu);  // commit -- must not touch the (unavailable) rtc

  TEST_ASSERT_EQUAL(kNow.hour(), rtc.now().hour());
}

void test_timezone_screen_cycles_selection() {
  AlarmClock alarms;
  alarms.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  Adafruit_ST7789 tft(0, 0, 0);
  TimezoneStore timezone;
  timezone.begin();
  TimeFormatStore timeFormat;
  timeFormat.begin();
  MenuSystem menu(tft, alarms, radio, nullptr, nullptr, timezone, timeFormat);
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
  RUN_TEST(test_holding_through_a_long_press_screen_change_does_not_cascade_further);
  RUN_TEST(test_radio_screen_tune_up_and_mute);
  RUN_TEST(test_radio_screen_does_nothing_when_no_radio_is_present);
  RUN_TEST(test_radio_screen_long_hold_seeks_instead_of_repeatedly_stepping);
  RUN_TEST(test_radio_screen_long_hold_fires_seek_even_off_the_repeat_schedule);
  RUN_TEST(test_alarm_ringing_on_another_screen_takes_over_home);
  RUN_TEST(test_radio_screen_refreshes_signal_periodically_without_a_button_press);
  RUN_TEST(test_radio_screen_holding_up_under_the_long_press_threshold_does_not_repeat_step);
  RUN_TEST(test_ringing_alarm_short_press_snoozes);
  RUN_TEST(test_ringing_alarm_long_press_dismisses);
  RUN_TEST(test_set_time_saves_the_new_hour_and_minute);
  RUN_TEST(test_set_time_day_field_wraps_to_1_past_the_months_last_day);
  RUN_TEST(test_set_time_format_field_toggles_between_24h_and_12h);
  RUN_TEST(test_set_time_sync_now_requests_sync_instead_of_saving);
  RUN_TEST(test_set_time_sync_now_does_nothing_while_offline);
  RUN_TEST(test_set_time_cancelled_with_long_press_does_not_save);
  RUN_TEST(test_set_time_with_no_rtc_does_not_crash);
  RUN_TEST(test_set_rtc_unavailable_skips_saving_even_with_a_non_null_rtc);
  RUN_TEST(test_timezone_screen_cycles_selection);
  return UNITY_END();
}

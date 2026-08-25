#include <unity.h>

#include "AlarmClock.h"
#include "AlarmSound.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "SI4735.h"
#include "WakeController.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  native_fake_millis_value() = 0;
}
void tearDown() {}

namespace {
// Fires alarm 0 (given the caller has already set it via clock.setAlarm)
// by driving the clock's own scheduling logic for real, rather than poking
// private state.
void ring(AlarmClock &clock, const Alarm &alarm) {
  clock.setAlarm(0, alarm);
  clock.update(DateTime(2026, 8, 25, alarm.hour, alarm.minute, 0));
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));
}
}  // namespace

void test_radio_wake_ramps_from_start_volume_to_target() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  radio.setVolume(30);  // this is the ramp target

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  native_fake_millis_value() = 1000;
  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));
  TEST_ASSERT_EQUAL(AlarmConfig::WakeRampStartVolume, radio.volume());
  TEST_ASSERT_FALSE(radio.muted());

  native_fake_millis_value() = 1000 + 45000;  // halfway through the 90s ramp
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 45));
  TEST_ASSERT_EQUAL(4 + (30 - 4) / 2, radio.volume());

  native_fake_millis_value() = 1000 + 90000;  // ramp complete
  wake.tickSlow(DateTime(2026, 8, 25, 7, 1, 31));
  TEST_ASSERT_EQUAL(30, radio.volume());
}

void test_radio_wake_falls_back_to_tone_on_dead_air() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  SI4735::setSimulatedRssi(2);  // below AlarmConfig::DeadAirRssiThreshold

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));

  native_fake_millis_value() = AlarmConfig::DeadAirCheckDelaySeconds * 1000UL;
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 5));

  TEST_ASSERT_TRUE(radio.muted());
  TEST_ASSERT_TRUE(sound.active());
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmSound::Tone::ClassicBeep),
                     static_cast<int>(sound.currentTone()));
}

void test_radio_wake_does_not_fall_back_with_good_signal() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  // Default simulated RSSI (50) is well above the threshold.

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));

  native_fake_millis_value() = AlarmConfig::DeadAirCheckDelaySeconds * 1000UL;
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 5));

  TEST_ASSERT_FALSE(radio.muted());
  TEST_ASSERT_FALSE(sound.active());
}

void test_beep_wake_mutes_radio_and_starts_tone_immediately() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::ClassicBeep;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));

  TEST_ASSERT_TRUE(radio.muted());
  TEST_ASSERT_TRUE(sound.active());
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmSound::Tone::ClassicBeep),
                     static_cast<int>(sound.currentTone()));
}

void test_chime_wake_selects_the_chime_tone() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Chime;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));

  TEST_ASSERT_EQUAL(static_cast<int>(AlarmSound::Tone::Chime),
                     static_cast<int>(sound.currentTone()));
}

void test_dismissing_a_radio_wake_restores_volume_and_unmutes() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  radio.setVolume(30);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));  // ramp starts, volume drops to 4

  clock.dismiss();
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 1));  // detects ring ended -> endWake()

  TEST_ASSERT_EQUAL(30, radio.volume());
  TEST_ASSERT_FALSE(radio.muted());
}

void test_dismissing_a_tone_wake_stops_the_tone_but_leaves_radio_muted() {
  AlarmClock clock;
  clock.begin();
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::ClassicBeep;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));
  TEST_ASSERT_TRUE(sound.active());

  clock.dismiss();
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 1));

  TEST_ASSERT_FALSE(sound.active());
  TEST_ASSERT_TRUE(radio.muted());  // deliberately left as-is, not this controller's job
}

void test_snoozing_ends_wake_and_re_ring_restarts_the_ramp() {
  AlarmClock clock;
  clock.begin();
  clock.setSnoozeMinutes(9);
  RadioTuner radio;
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  radio.setVolume(30);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  DateTime ringTime(2026, 8, 25, 7, 0, 0);
  wake.tickSlow(ringTime);
  TEST_ASSERT_EQUAL(AlarmConfig::WakeRampStartVolume, radio.volume());

  clock.snooze(ringTime);
  wake.tickSlow(ringTime);  // detects ring ended -> endWake(), restores volume
  TEST_ASSERT_EQUAL(30, radio.volume());

  DateTime dueTime = ringTime + TimeSpan(0, 0, 9, 0);
  clock.update(dueTime);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));

  wake.tickSlow(dueTime);  // new ring -> beginWake() again, ramp restarts
  TEST_ASSERT_EQUAL(AlarmConfig::WakeRampStartVolume, radio.volume());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_radio_wake_ramps_from_start_volume_to_target);
  RUN_TEST(test_radio_wake_falls_back_to_tone_on_dead_air);
  RUN_TEST(test_radio_wake_does_not_fall_back_with_good_signal);
  RUN_TEST(test_beep_wake_mutes_radio_and_starts_tone_immediately);
  RUN_TEST(test_chime_wake_selects_the_chime_tone);
  RUN_TEST(test_dismissing_a_radio_wake_restores_volume_and_unmutes);
  RUN_TEST(test_dismissing_a_tone_wake_stops_the_tone_but_leaves_radio_muted);
  RUN_TEST(test_snoozing_ends_wake_and_re_ring_restarts_the_ramp);
  return UNITY_END();
}

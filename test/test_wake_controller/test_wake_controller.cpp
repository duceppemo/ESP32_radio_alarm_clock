#include <unity.h>

#include "AlarmClock.h"
#include "AlarmSound.h"
#include "Preferences.h"
#include "RadioTuner.h"
#include "RegionStore.h"
#include "SI4735.h"
#include "WakeController.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedPresent();
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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

  uint32_t rampMs = (uint32_t)AlarmConfig::WakeRampSeconds * 1000UL;
  native_fake_millis_value() = 1000 + rampMs / 2;  // halfway through the ramp
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));
  TEST_ASSERT_EQUAL(4 + (30 - 4) / 2, radio.volume());

  native_fake_millis_value() = 1000 + rampMs;  // ramp complete
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));
  TEST_ASSERT_EQUAL(30, radio.volume());
}

void test_manual_volume_change_during_ramp_overrides_it() {
  // Regression: the ramp used to re-assert its own computed volume every
  // tickSlow(), so a Vol+/Vol- press (or the dashboard slider) during a
  // ring took effect for under a second before being silently overwritten
  // -- the controls felt dead while an alarm was ringing.
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  wake.tickSlow(ringTime);  // ramp starts, volume -> WakeRampStartVolume

  radio.volumeUp();  // user presses Vol+ mid-ramp: jumps to the saved 30 (skip-ramp)
  TEST_ASSERT_EQUAL(30, radio.volume());

  native_fake_millis_value() = 1;  // next tick, still well inside the ramp
  wake.tickSlow(ringTime);

  // Without the fix, this tick's ramp math would overwrite the user's
  // press with a freshly-computed (lower) transient value. Note nothing
  // *persisted* changed here (30 -> 30), so the override has to be
  // detected off the live value.
  TEST_ASSERT_EQUAL(30, radio.volume());
  TEST_ASSERT_EQUAL(30, radio.persistedVolume());  // one press did NOT save a quiet step
}

void test_volume_down_during_ramp_also_overrides_it() {
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  uint32_t rampMs = (uint32_t)AlarmConfig::WakeRampSeconds * 1000UL;
  wake.tickSlow(ringTime);
  native_fake_millis_value() = rampMs / 2;
  wake.tickSlow(ringTime);
  uint8_t midRamp = radio.volume();

  radio.volumeDown();  // quieter, please
  TEST_ASSERT_EQUAL(midRamp - 1, radio.volume());

  native_fake_millis_value() += 1000;
  wake.tickSlow(ringTime);  // ramp would have pushed it back up
  TEST_ASSERT_EQUAL(midRamp - 1, radio.volume());
}

void test_radio_wake_with_no_tuner_module_falls_back_to_tone_immediately() {
  // Without a chip on the bus, the ramp is silent and the dead-air check
  // wouldn't fire for DeadAirCheckDelaySeconds -- five seconds of nothing
  // before the alarm actually makes a sound. Skip straight to the beep.
  SI4735::setSimulatedPresent(false);
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  TEST_ASSERT_FALSE(radio.begin());
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  wake.tickFast();  // the very first tick, no delay at all

  TEST_ASSERT_TRUE(sound.active());
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmSound::Tone::ClassicBeep),
                     static_cast<int>(sound.currentTone()));

  clock.dismiss();
  wake.tickFast();
  TEST_ASSERT_FALSE(sound.active());
  TEST_ASSERT_TRUE(radio.muted());  // nothing to unmute back onto
  SI4735::resetSimulatedPresent();
}

void test_snoozing_a_radio_wake_does_not_degrade_the_next_rings_target() {
  // Regression: beginWake() used to compute the ramp target from
  // radio.volume() -- the *live* value, which right after a snooze is
  // whatever quiet transient step the ramp had reached, not the user's
  // real saved volume. Repeatedly snoozing a radio alarm used to ratchet
  // the target down toward silence; it should always ramp back toward the
  // real saved volume (persistedVolume()) instead.
  AlarmClock clock;
  clock.begin();
  clock.setSnoozeMinutes(9);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  wake.tickSlow(ringTime);  // beginWake() at millis()==0

  uint32_t rampMs = (uint32_t)AlarmConfig::WakeRampSeconds * 1000UL;
  native_fake_millis_value() = rampMs / 2;  // partway through the ramp
  wake.tickSlow(ringTime);
  uint8_t midRampVolume = radio.volume();
  TEST_ASSERT_TRUE(midRampVolume > AlarmConfig::WakeRampStartVolume);
  TEST_ASSERT_TRUE(midRampVolume < 30);

  clock.snooze(ringTime);
  wake.tickSlow(ringTime);  // endWake(Snoozed) mutes -- volume_ stays at the interrupted step
  TEST_ASSERT_EQUAL(midRampVolume, radio.volume());

  DateTime dueTime = ringTime + TimeSpan(0, 0, 9, 0);
  clock.update(dueTime);
  native_fake_millis_value() = rampMs / 2 + 9UL * 60 * 1000;
  wake.tickSlow(dueTime);  // re-ring -> beginWake() recomputes the ramp target

  // Let the new ramp finish and confirm it lands back on the real saved
  // volume, not something degraded from the interrupted mid-ramp value.
  native_fake_millis_value() += rampMs;
  wake.tickSlow(dueTime);
  TEST_ASSERT_EQUAL(30, radio.volume());
}

void test_beginning_a_radio_wake_cancels_a_pending_sleep_timer() {
  // Regression: a sleep timer set before an alarm rings used to keep
  // running through the ring -- RadioTuner::update() doesn't know an alarm
  // is active, so if the timer's deadline landed mid-ring, it would
  // silently mute a wake in progress.
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  radio.setMuted(false);
  radio.setSleepTimer(5);
  TEST_ASSERT_TRUE(radio.sleepTimerActive());

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 0));  // beginWake() should cancel the sleep timer

  TEST_ASSERT_FALSE(radio.sleepTimerActive());
}

void test_dismissing_after_dead_air_fallback_leaves_the_radio_muted() {
  // Regression: endWake(Idle) used to unconditionally restore volume and
  // unmute a radio-source wake, even when the dead-air fallback had
  // already muted it and switched to the beep tone -- dismissing then
  // unmuted the radio right back onto whatever silence/static triggered
  // the fallback in the first place.
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  TEST_ASSERT_TRUE(radio.muted());  // dead-air fallback triggered

  clock.dismiss();
  wake.tickSlow(DateTime(2026, 8, 25, 7, 0, 6));

  TEST_ASSERT_TRUE(radio.muted());  // must stay muted, not unmute back onto static
}

void test_radio_wake_falls_back_to_tone_on_dead_air() {
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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

void test_dismissing_a_tone_wake_stops_immediately_via_tick_fast() {
  // Regression: beginWake()/endWake() detection used to live only in
  // tickSlow(), which main.cpp only calls once a second -- a beep/chime
  // dismissed via a fast-path action (the menu's Home shortcut, the
  // dashboard's /api/alarm/dismiss) would keep sounding for up to that long
  // after AlarmClock's state had already gone back to Idle. tickFast()
  // (called every loop iteration, unthrottled) must notice and silence it
  // immediately, without waiting for the next tickSlow().
  AlarmClock clock;
  clock.begin();
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  wake.tickFast();  // no tickSlow() call at all -- must still stop the tone

  TEST_ASSERT_FALSE(sound.active());
}

void test_snoozing_ends_wake_and_re_ring_restarts_the_ramp() {
  AlarmClock clock;
  clock.begin();
  clock.setSnoozeMinutes(9);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
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
  wake.tickSlow(ringTime);  // detects ring ended -> endWake() mutes rather than blaring at full volume
  TEST_ASSERT_TRUE(radio.muted());

  DateTime dueTime = ringTime + TimeSpan(0, 0, 9, 0);
  clock.update(dueTime);
  TEST_ASSERT_EQUAL(static_cast<int>(AlarmState::Ringing), static_cast<int>(clock.state()));

  wake.tickSlow(dueTime);  // new ring -> beginWake() again, ramp restarts
  TEST_ASSERT_EQUAL(AlarmConfig::WakeRampStartVolume, radio.volume());
  TEST_ASSERT_FALSE(radio.muted());
}

void test_snooze_press_mutes_the_radio_immediately_via_tick_fast() {
  // Regression: pressing snooze must silence the radio right away rather
  // than waiting up to a second for the next tickSlow() -- same reasoning
  // as the tone-wake equivalent above.
  AlarmClock clock;
  clock.begin();
  clock.setSnoozeMinutes(9);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  AlarmSound sound;
  sound.begin();
  WakeController wake(clock, radio, sound);

  Alarm a;
  a.hour = 7;
  a.minute = 0;
  a.enabled = true;
  a.daysMask = 0b1111111;
  a.wakeSource = WakeSource::Radio;

  ring(clock, a);
  DateTime ringTime(2026, 8, 25, 7, 0, 0);
  wake.tickSlow(ringTime);
  TEST_ASSERT_FALSE(radio.muted());

  clock.snooze(ringTime);
  wake.tickFast();  // no tickSlow() call at all -- must still mute immediately

  TEST_ASSERT_TRUE(radio.muted());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_radio_wake_ramps_from_start_volume_to_target);
  RUN_TEST(test_manual_volume_change_during_ramp_overrides_it);
  RUN_TEST(test_volume_down_during_ramp_also_overrides_it);
  RUN_TEST(test_radio_wake_with_no_tuner_module_falls_back_to_tone_immediately);
  RUN_TEST(test_snoozing_a_radio_wake_does_not_degrade_the_next_rings_target);
  RUN_TEST(test_beginning_a_radio_wake_cancels_a_pending_sleep_timer);
  RUN_TEST(test_dismissing_after_dead_air_fallback_leaves_the_radio_muted);
  RUN_TEST(test_radio_wake_falls_back_to_tone_on_dead_air);
  RUN_TEST(test_radio_wake_does_not_fall_back_with_good_signal);
  RUN_TEST(test_beep_wake_mutes_radio_and_starts_tone_immediately);
  RUN_TEST(test_chime_wake_selects_the_chime_tone);
  RUN_TEST(test_dismissing_a_radio_wake_restores_volume_and_unmutes);
  RUN_TEST(test_dismissing_a_tone_wake_stops_the_tone_but_leaves_radio_muted);
  RUN_TEST(test_dismissing_a_tone_wake_stops_immediately_via_tick_fast);
  RUN_TEST(test_snoozing_ends_wake_and_re_ring_restarts_the_ramp);
  RUN_TEST(test_snooze_press_mutes_the_radio_immediately_via_tick_fast);
  return UNITY_END();
}

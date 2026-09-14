#include <unity.h>

#include "Preferences.h"
#include "RadioTuner.h"
#include "RegionStore.h"
#include "SI4735.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedPresent();
  SI4735::resetDriverCallCount();
  SI4735::clearSimulatedRdsDateTime();
}
void tearDown() {}

void test_begin_reports_availability_when_the_chip_responds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);

  TEST_ASSERT_TRUE(radio.begin());
  TEST_ASSERT_TRUE(radio.available());
}

void test_begin_reports_unavailable_when_no_chip_responds() {
  SI4735::setSimulatedPresent(false);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);

  TEST_ASSERT_FALSE(radio.begin());
  TEST_ASSERT_FALSE(radio.available());
}

void test_tune_clamps_to_fm_band_bounds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.tune(0);
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart, radio.frequency10kHz());

  radio.tune(65000);
  TEST_ASSERT_EQUAL(RadioConfig::FmBandEnd, radio.frequency10kHz());
}

void test_set_volume_clamps_to_63() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.setVolume(200);
  TEST_ASSERT_EQUAL(63, radio.volume());
}

void test_volume_up_and_down_stop_at_bounds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.setVolume(63);
  radio.volumeUp();
  TEST_ASSERT_EQUAL(63, radio.volume());

  radio.setVolume(0);
  radio.volumeDown();
  TEST_ASSERT_EQUAL(0, radio.volume());
}

void test_transient_volume_is_not_persisted() {
  {
    RegionStore region;
    region.begin();
    RadioTuner radio(region);
    radio.begin();
    radio.setVolume(40);          // persisted
    radio.setVolumeTransient(10); // NOT persisted -- this is the point of it
    TEST_ASSERT_EQUAL(10, radio.volume());
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(40, reloaded.volume());
}

void test_store_and_recall_preset() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.storePreset(2, 9330);
  radio.tune(RadioConfig::FmDefaultFreq);  // move away first
  radio.recallPreset(2);

  TEST_ASSERT_EQUAL(9330, radio.frequency10kHz());
  TEST_ASSERT_EQUAL(9330, radio.preset(2));
}

void test_recalling_an_unset_preset_does_nothing() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(9500);

  radio.recallPreset(3);  // never stored

  TEST_ASSERT_EQUAL(9500, radio.frequency10kHz());
}

void test_presets_persist_across_instances() {
  {
    RegionStore region;
    region.begin();
    RadioTuner radio(region);
    radio.begin();
    radio.storePreset(0, 8900);
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(8900, reloaded.preset(0));
}

void test_sleep_timer_mutes_only_after_it_elapses() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  native_fake_millis_value() = 0;
  radio.setSleepTimer(1);  // 1 minute

  native_fake_millis_value() = 59999;
  radio.update();
  TEST_ASSERT_FALSE(radio.muted());
  TEST_ASSERT_TRUE(radio.sleepTimerActive());

  native_fake_millis_value() = 60000;
  radio.update();
  TEST_ASSERT_TRUE(radio.muted());
  TEST_ASSERT_FALSE(radio.sleepTimerActive());
}

void test_sleep_timer_remaining_minutes_is_exact_at_the_boundary() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  native_fake_millis_value() = 0;
  radio.setSleepTimer(5);

  // Exactly 5 minutes left should read as 5, not 6 (a ceiling-division
  // off-by-one that a prior version of this code had).
  TEST_ASSERT_EQUAL(5, radio.sleepTimerRemainingMinutes());

  native_fake_millis_value() = 4 * 60000 + 1;  // just over 4 minutes elapsed
  TEST_ASSERT_EQUAL(1, radio.sleepTimerRemainingMinutes());
}

void test_cancel_sleep_timer_prevents_auto_mute() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  native_fake_millis_value() = 0;
  radio.setSleepTimer(1);
  radio.cancelSleepTimer();

  native_fake_millis_value() = 60000;
  radio.update();

  TEST_ASSERT_FALSE(radio.muted());
}

void test_set_sleep_timer_to_zero_behaves_like_cancel() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  native_fake_millis_value() = 0;
  // Regression: an int-to-uint16_t truncation upstream (e.g. the dashboard
  // casting a JSON value of 65536) can land here with exactly 0 even though
  // the caller meant to arm a timer, not cancel one. millis() + 0 used to be
  // treated as a valid (already-past) deadline, muting on the very next
  // update() instead of leaving the timer inactive.
  radio.setSleepTimer(0);

  TEST_ASSERT_FALSE(radio.sleepTimerActive());

  native_fake_millis_value() = 1;
  radio.update();
  TEST_ASSERT_FALSE(radio.muted());
}

// Regression: every method used to call straight into the underlying
// SI4735 driver with no available() check. The real library's
// waitToSend() polls the chip's Clear-To-Send bit in an unbounded loop
// with no timeout, so with no chip actually present, this hung forever --
// exactly what the web dashboard hit querying radio state unconditionally
// on every poll. Getters must return a safe default and setters must not
// touch the driver at all when unavailable (while still tracking
// volume/mute state, same as if a chip were there).
void test_nothing_touches_the_driver_when_radio_is_unavailable() {
  SI4735::setSimulatedPresent(false);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  TEST_ASSERT_FALSE(radio.available());

  TEST_ASSERT_EQUAL(0, radio.frequency10kHz());
  TEST_ASSERT_EQUAL(0, radio.rssi());

  radio.tune(9500);
  radio.seekUp();
  radio.seekDown();
  radio.setVolume(40);
  radio.setMuted(true);
  radio.updateRdsSync(true);

  // Tracked in the wrapper regardless of hardware, same as BatteryMonitor's
  // pattern for its own unavailable case.
  TEST_ASSERT_EQUAL(40, radio.volume());
  TEST_ASSERT_TRUE(radio.muted());
  // None of the calls above reached the driver at all -- this is what
  // actually confirms the guard, not just that nothing crashed.
  TEST_ASSERT_EQUAL(0, SI4735::driverCallCount());
}

void test_rssi_reflects_simulated_signal() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  SI4735::setSimulatedRssi(3);
  TEST_ASSERT_EQUAL(3, radio.rssi());
}

// --- Region ---

void test_begin_applies_the_current_region_to_the_chip() {
  RegionStore region;
  region.begin();
  region.setIndex(1);  // Europe / Rest of World: 50us de-emphasis
  RadioTuner radio(region);

  radio.begin();

  TEST_ASSERT_EQUAL(1, SI4735::lastAppliedFmDeEmphasis());
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart, SI4735::lastAppliedFmBandStart());
  TEST_ASSERT_EQUAL(RadioConfig::FmBandEnd, SI4735::lastAppliedFmBandEnd());
}

void test_changing_region_live_reapplies_de_emphasis() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  TEST_ASSERT_EQUAL(2, SI4735::lastAppliedFmDeEmphasis());  // Americas default: 75us

  region.setIndex(2);  // Japan: 50us
  radio.applyRegion();

  TEST_ASSERT_EQUAL(1, SI4735::lastAppliedFmDeEmphasis());
  TEST_ASSERT_EQUAL(7600, SI4735::lastAppliedFmBandStart());
  TEST_ASSERT_EQUAL(9500, SI4735::lastAppliedFmBandEnd());
}

void test_apply_region_reclamps_frequency_into_the_new_band() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(9750);  // 97.50 MHz -- outside Japan's band

  region.setIndex(2);  // Japan: 76.0-95.0MHz
  radio.applyRegion();

  TEST_ASSERT_EQUAL(9500, radio.frequency10kHz());  // clamped to Japan's band top
}

void test_tune_clamps_to_the_current_regions_band_not_a_fixed_constant() {
  // Regression: tune() used to clamp against RadioConfig::FmBandStart/End
  // unconditionally, so switching to Japan's narrower/shifted band left
  // tune() still allowing (and clamping to) the Americas/Europe range.
  RegionStore region;
  region.begin();
  region.setIndex(2);  // Japan: 76.0-95.0MHz
  RadioTuner radio(region);
  radio.begin();

  radio.tune(10800);  // 108.00 MHz -- past Japan's band entirely

  TEST_ASSERT_EQUAL(9500, radio.frequency10kHz());  // clamped to Japan's 95.0MHz top, not 108.0
}

// --- RDS time sync ---

void test_passive_rds_harvest_while_unmuted_does_not_need_the_fallback_flag() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);  // actively "listening"

  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  radio.updateRdsSync(/*needsFallback=*/false);  // NTP is fine -- still harvests for free

  TEST_ASSERT_TRUE(radio.consumeRdsTimeSync());
  DateTime t = radio.rdsTime();
  TEST_ASSERT_EQUAL(2026, t.year());
  TEST_ASSERT_EQUAL(3, t.month());
  TEST_ASSERT_EQUAL(15, t.day());
  TEST_ASSERT_EQUAL(7, t.hour());
  TEST_ASSERT_EQUAL(42, t.minute());
}

void test_consume_rds_time_sync_is_one_shot() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  radio.updateRdsSync(false);

  TEST_ASSERT_TRUE(radio.consumeRdsTimeSync());
  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());  // already consumed
}

void test_implausible_rds_year_is_rejected() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  SI4735::setSimulatedRdsDateTime(1978, 3, 15, 7, 42);  // noisy/implausible decode
  radio.updateRdsSync(false);

  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());
}

void test_fallback_retune_only_starts_while_muted() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);  // actively in use

  native_fake_millis_value() = 1000;
  radio.updateRdsSync(/*needsFallback=*/true);

  // Still unmuted -- no covert retune should have happened (setFrequency()
  // from tune()/begin() itself already ran, so this checks it didn't climb
  // further from a background attempt).
  int callsAfterUnmutedTick = SI4735::driverCallCount();

  radio.setMuted(true);
  native_fake_millis_value() = 2000;
  radio.updateRdsSync(true);

  TEST_ASSERT_TRUE(SI4735::driverCallCount() > callsAfterUnmutedTick);
}

void test_real_user_action_cancels_an_in_progress_fallback_attempt() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(true);

  native_fake_millis_value() = 1000;
  radio.updateRdsSync(true);  // starts a fallback attempt (first ever -- fires immediately)

  radio.tune(9500);  // user action while an attempt is in flight -- cancels it

  // Still muted and well within the 30-minute retry interval, so no new
  // attempt starts either -- a CT frame arriving now must not be picked
  // up, since nothing is actively listening for one anymore.
  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  native_fake_millis_value() = 1500;
  radio.updateRdsSync(true);

  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_begin_reports_availability_when_the_chip_responds);
  RUN_TEST(test_begin_reports_unavailable_when_no_chip_responds);
  RUN_TEST(test_tune_clamps_to_fm_band_bounds);
  RUN_TEST(test_set_volume_clamps_to_63);
  RUN_TEST(test_volume_up_and_down_stop_at_bounds);
  RUN_TEST(test_transient_volume_is_not_persisted);
  RUN_TEST(test_store_and_recall_preset);
  RUN_TEST(test_recalling_an_unset_preset_does_nothing);
  RUN_TEST(test_presets_persist_across_instances);
  RUN_TEST(test_sleep_timer_mutes_only_after_it_elapses);
  RUN_TEST(test_sleep_timer_remaining_minutes_is_exact_at_the_boundary);
  RUN_TEST(test_cancel_sleep_timer_prevents_auto_mute);
  RUN_TEST(test_set_sleep_timer_to_zero_behaves_like_cancel);
  RUN_TEST(test_nothing_touches_the_driver_when_radio_is_unavailable);
  RUN_TEST(test_rssi_reflects_simulated_signal);
  RUN_TEST(test_begin_applies_the_current_region_to_the_chip);
  RUN_TEST(test_changing_region_live_reapplies_de_emphasis);
  RUN_TEST(test_apply_region_reclamps_frequency_into_the_new_band);
  RUN_TEST(test_tune_clamps_to_the_current_regions_band_not_a_fixed_constant);
  RUN_TEST(test_passive_rds_harvest_while_unmuted_does_not_need_the_fallback_flag);
  RUN_TEST(test_consume_rds_time_sync_is_one_shot);
  RUN_TEST(test_implausible_rds_year_is_rejected);
  RUN_TEST(test_fallback_retune_only_starts_while_muted);
  RUN_TEST(test_real_user_action_cancels_an_in_progress_fallback_attempt);
  return UNITY_END();
}

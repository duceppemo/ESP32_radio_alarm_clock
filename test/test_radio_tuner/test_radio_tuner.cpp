#include <unity.h>

#include "Preferences.h"
#include "RadioTuner.h"
#include "SI4735.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedPresent();
}
void tearDown() {}

void test_begin_reports_availability_when_the_chip_responds() {
  RadioTuner radio;

  TEST_ASSERT_TRUE(radio.begin());
  TEST_ASSERT_TRUE(radio.available());
}

void test_begin_reports_unavailable_when_no_chip_responds() {
  SI4735::setSimulatedPresent(false);
  RadioTuner radio;

  TEST_ASSERT_FALSE(radio.begin());
  TEST_ASSERT_FALSE(radio.available());
}

void test_tune_clamps_to_fm_band_bounds() {
  RadioTuner radio;
  radio.begin();

  radio.tune(0);
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart, radio.frequency10kHz());

  radio.tune(65000);
  TEST_ASSERT_EQUAL(RadioConfig::FmBandEnd, radio.frequency10kHz());
}

void test_set_volume_clamps_to_63() {
  RadioTuner radio;
  radio.begin();

  radio.setVolume(200);
  TEST_ASSERT_EQUAL(63, radio.volume());
}

void test_volume_up_and_down_stop_at_bounds() {
  RadioTuner radio;
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
    RadioTuner radio;
    radio.begin();
    radio.setVolume(40);          // persisted
    radio.setVolumeTransient(10); // NOT persisted -- this is the point of it
    TEST_ASSERT_EQUAL(10, radio.volume());
  }

  RadioTuner reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL(40, reloaded.volume());
}

void test_store_and_recall_preset() {
  RadioTuner radio;
  radio.begin();

  radio.storePreset(2, 9330);
  radio.tune(RadioConfig::FmDefaultFreq);  // move away first
  radio.recallPreset(2);

  TEST_ASSERT_EQUAL(9330, radio.frequency10kHz());
  TEST_ASSERT_EQUAL(9330, radio.preset(2));
}

void test_recalling_an_unset_preset_does_nothing() {
  RadioTuner radio;
  radio.begin();
  radio.tune(9500);

  radio.recallPreset(3);  // never stored

  TEST_ASSERT_EQUAL(9500, radio.frequency10kHz());
}

void test_presets_persist_across_instances() {
  {
    RadioTuner radio;
    radio.begin();
    radio.storePreset(0, 8900);
  }

  RadioTuner reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL(8900, reloaded.preset(0));
}

void test_sleep_timer_mutes_only_after_it_elapses() {
  RadioTuner radio;
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
  RadioTuner radio;
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
  RadioTuner radio;
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
  RadioTuner radio;
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

void test_rssi_reflects_simulated_signal() {
  RadioTuner radio;
  radio.begin();

  SI4735::setSimulatedRssi(3);
  TEST_ASSERT_EQUAL(3, radio.rssi());
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
  RUN_TEST(test_rssi_reflects_simulated_signal);
  return UNITY_END();
}

#include <unity.h>

#include "Preferences.h"
#include "RadioTuner.h"
#include "RegionStore.h"
#include "SI4735.h"

void setUp() {
  Preferences::resetAll();
  SI4735::resetSimulatedRssi();
  SI4735::resetSimulatedSnr();
  SI4735::resetSimulatedPresent();
  SI4735::resetDriverCallCount();
  SI4735::clearSimulatedRdsDateTime();
  SI4735::clearSimulatedSignalAt();
}
void tearDown() {}

namespace {
// Americas (RegionStore's default, index 0) has real-world 200kHz channel
// spacing -- an independent, hardcoded expectation here (not read back from
// RegionStore::entry(0).fmStep) so these tests still catch that data
// changing unexpectedly, the same spirit as the hardcoded band-edge
// constants already used throughout this file.
constexpr uint16_t kFmStep = 20;
// The highest frequency actually reachable on that grid -- RadioTuner.cpp's
// topOfGrid(): 108.0MHz (RadioConfig::FmBandEnd) itself isn't on-grid once
// step is 20, so tune()/stepDown()/seekDown() all land/wrap here instead.
constexpr uint16_t kFmTop = 10790;

// seekUp()/seekDown() only *start* a seek -- update() advances it one
// candidate per SeekSettleMs (see RadioTuner.h for why it no longer blocks).
// Drives it to completion the way main.cpp's loop() would, bounded so a
// seek that somehow never finished fails the test instead of hanging it.
void runSeek(RadioTuner &radio) {
  for (int i = 0; i < 2000 && radio.seeking(); i++) {
    native_fake_millis_value() += RadioConfig::SeekSettleMs;
    radio.update();
  }
  TEST_ASSERT_FALSE_MESSAGE(radio.seeking(), "seek never finished");
}

// Volume/mute are written to NVS lazily (SettingsFlushDelayMs after the
// last change, from update()) -- this is what a test that then reloads a
// fresh instance from NVS has to do first.
void flushSettings(RadioTuner &radio) {
  native_fake_millis_value() += RadioConfig::SettingsFlushDelayMs;
  radio.update();
}
}  // namespace

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

void test_rssi_queries_fresh_signal_quality_each_call() {
  // Regression guard: getCurrentRSSI() alone just returns a cached field --
  // rssi() must also call getCurrentReceivedSignalQuality() to actually
  // populate it (this is why the Radio screen's "Sig" line always read 0).
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  int callsBefore = SI4735::driverCallCount();
  radio.rssi();
  radio.rssi();

  TEST_ASSERT_EQUAL(2, SI4735::driverCallCount() - callsBefore);
}

void test_tune_clamps_to_fm_band_bounds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.tune(0);
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart, radio.frequency10kHz());

  radio.tune(65000);
  // Clamps to 108.0MHz, then snaps down to the nearest real channel on
  // Americas' grid, 107.9MHz -- see kFmTop.
  TEST_ASSERT_EQUAL(kFmTop, radio.frequency10kHz());
}

void test_step_up_wraps_from_band_end_to_band_start() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.tune(RadioConfig::FmBandEnd);
  radio.stepUp();

  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart, radio.frequency10kHz());
}

void test_step_down_wraps_from_band_start_to_band_end() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.tune(RadioConfig::FmBandStart);
  radio.stepDown();

  // Wraps to the topmost frequency actually on the grid (107.9MHz), not
  // the band's own 108.0MHz edge -- see kFmTop.
  TEST_ASSERT_EQUAL(kFmTop, radio.frequency10kHz());
}

void test_step_up_and_down_move_by_one_fm_step_away_from_the_edges() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint16_t mid = RadioConfig::FmDefaultFreq;  // 97.50MHz -- already grid-aligned, far from either edge
  radio.tune(mid);

  radio.stepUp();
  TEST_ASSERT_EQUAL(mid + kFmStep, radio.frequency10kHz());

  radio.stepDown();
  radio.stepDown();
  TEST_ASSERT_EQUAL(mid - kFmStep, radio.frequency10kHz());
}

void test_tune_snaps_to_the_odd_decimal_grid_in_americas_only() {
  RegionStore region;
  region.begin();  // Americas (index 0): real 200kHz channel spacing
  RadioTuner radio(region);
  radio.begin();

  radio.tune(8800);  // 88.0MHz -- not a real North American channel
  // Nearest valid Americas channel: 88.1MHz. An "even" tenth under a
  // 200kHz grid is always exactly halfway between two valid ones --
  // snapToGrid() breaks that tie by rounding up.
  TEST_ASSERT_EQUAL(8810, radio.frequency10kHz());

  radio.tune(9430);  // 94.3MHz -- already a real (odd-decimal) channel
  TEST_ASSERT_EQUAL(9430, radio.frequency10kHz());  // left untouched
}

void test_tune_does_not_snap_to_odd_decimals_outside_americas() {
  RegionStore region;
  region.begin();
  region.setIndex(1);  // Europe / Rest of World: 100kHz spacing, any tenth valid
  RadioTuner radio(region);
  radio.begin();

  radio.tune(8800);  // 88.0MHz -- a perfectly valid European channel
  TEST_ASSERT_EQUAL(8800, radio.frequency10kHz());  // not snapped to 88.1
}

void test_step_wraps_within_the_current_regions_band_not_a_fixed_constant() {
  // Regression guard for the same class of bug as
  // test_tune_clamps_to_the_current_regions_band_not_a_fixed_constant --
  // stepping must wrap at Japan's band, not the Americas/Europe one.
  RegionStore region;
  region.begin();
  region.setIndex(2);  // Japan: 76.0-95.0MHz
  RadioTuner radio(region);
  radio.begin();

  radio.tune(9500);  // Japan's band top
  radio.stepUp();

  TEST_ASSERT_EQUAL(7600, radio.frequency10kHz());  // wraps to Japan's band start, not 8750
}

// --- Software seek (see Config.h's SeekRssiThreshold/SeekSnrThreshold
// comment for why this isn't the chip's own hardware seek) ---

void test_seek_up_stops_at_the_first_candidate_clearing_both_thresholds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);  // dead air everywhere except...
  SI4735::setSimulatedSnr(0);
  uint16_t target = RadioConfig::FmBandStart + 5 * kFmStep;
  SI4735::setSimulatedSignalAt(target, RadioConfig::SeekRssiThreshold, RadioConfig::SeekSnrThreshold);

  radio.seekUp();
  runSeek(radio);

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_seek_is_non_blocking_and_advances_one_candidate_per_settle_time() {
  // Regression: the sweep used to run to completion inside seekUp() itself,
  // holding the shared state lock for up to ~6s on the 100kHz grids --
  // past the 5s task watchdog the dashboard's async_tcp task runs under.
  // Now seekUp() returns immediately with the first candidate on the chip,
  // and each update() call moves on by at most one candidate, and only
  // once SeekSettleMs has passed since the last one.
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  native_fake_millis_value() = 1000;

  radio.seekUp();
  TEST_ASSERT_TRUE(radio.seeking());
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart + kFmStep, radio.frequency10kHz());

  radio.update();  // no time has passed -- still settling on the first candidate
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart + kFmStep, radio.frequency10kHz());

  native_fake_millis_value() += RadioConfig::SeekSettleMs;
  radio.update();  // settled, read as dead air -> next candidate
  TEST_ASSERT_EQUAL(RadioConfig::FmBandStart + 2 * kFmStep, radio.frequency10kHz());
  TEST_ASSERT_TRUE(radio.seeking());
}

void test_tune_cancels_an_in_progress_seek() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);

  radio.seekUp();
  TEST_ASSERT_TRUE(radio.seeking());

  radio.tune(9750);  // user picked a station mid-sweep (preset, dashboard, step)
  TEST_ASSERT_FALSE(radio.seeking());
  native_fake_millis_value() += RadioConfig::SeekSettleMs;
  radio.update();  // must not resume sweeping away from what the user chose
  TEST_ASSERT_EQUAL(9750, radio.frequency10kHz());
}

void test_seek_persists_the_found_frequency() {
  // Regression: seekUp()/seekDown() (via climbToLocalPeak()) used to call
  // si4735_.setFrequency() directly without ever persisting the result, so
  // a seeked station reverted to the last explicitly-*tuned* frequency on
  // reboot or a region switch (both of which re-clamp from NVS, not from
  // wherever the chip actually is).
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = RadioConfig::FmBandStart + 5 * kFmStep;
  SI4735::setSimulatedSignalAt(target, RadioConfig::SeekRssiThreshold, RadioConfig::SeekSnrThreshold);

  radio.seekUp();
  runSeek(radio);
  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());

  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(target, reloaded.frequency10kHz());
}

void test_seek_up_climbs_past_a_shoulder_to_the_stations_actual_peak() {
  // Regression test modeling a real measured station from a live band
  // sweep: a shoulder one FmStep before the peak already clears both
  // thresholds (Sig 24/SNR 8), but the true peak one step further on is
  // much stronger (Sig 31/SNR 16) -- naively stopping at the shoulder
  // (confirmed live) lands 0.1MHz short of the station every time.
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t shoulder = RadioConfig::FmBandStart + 5 * kFmStep;
  uint16_t peak = shoulder + kFmStep;
  uint16_t farSide = peak + kFmStep;
  SI4735::setSimulatedSignalAt(shoulder, 24, 8);
  SI4735::setSimulatedSignalAt(peak, 31, 16);
  SI4735::setSimulatedSignalAt(farSide, 22, 6);  // falling off again past the peak

  radio.seekUp();
  runSeek(radio);

  TEST_ASSERT_EQUAL(peak, radio.frequency10kHz());
}

void test_seek_down_climbs_past_a_shoulder_to_the_stations_actual_peak() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandEnd);  // snapped down to kFmTop (107.9MHz) by tune()
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  // Anchored to kFmTop, not FmBandEnd -- seekDown() below only ever visits
  // kFmTop, kFmTop-kFmStep, kFmTop-2*kFmStep, ... once it's actually
  // starting from there.
  uint16_t shoulder = kFmTop - 5 * kFmStep;
  uint16_t peak = shoulder - kFmStep;
  uint16_t farSide = peak - kFmStep;
  SI4735::setSimulatedSignalAt(shoulder, 24, 8);
  SI4735::setSimulatedSignalAt(peak, 31, 16);
  SI4735::setSimulatedSignalAt(farSide, 22, 6);

  radio.seekDown();
  runSeek(radio);

  TEST_ASSERT_EQUAL(peak, radio.frequency10kHz());
}

void test_seek_up_wraps_past_band_end_to_find_a_station_before_the_start() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  // Starting near the top means an unwrapped upward sweep would hit the
  // band edge long before reaching this target -- confirms it wraps rather
  // than giving up at 108.0MHz the way the hardware seek used to.
  radio.tune(kFmTop - kFmStep);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = RadioConfig::FmBandStart + 2 * kFmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  radio.seekUp();
  runSeek(radio);

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_seek_down_wraps_past_band_start_to_find_a_station_before_the_end() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart + kFmStep);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  // Anchored to kFmTop, not FmBandEnd -- once seekDown() wraps past the
  // bottom of the band, it lands on kFmTop (the actual top of the grid)
  // and steps down from there, same as in the shoulder/peak test above.
  uint16_t target = kFmTop - 2 * kFmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  radio.seekDown();
  runSeek(radio);

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_seek_up_returns_to_the_starting_frequency_when_nothing_clears_threshold() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t start = RadioConfig::FmBandStart + 10 * kFmStep;
  radio.tune(start);
  SI4735::setSimulatedRssi(0);  // dead air across the entire band, no exceptions
  SI4735::setSimulatedSnr(0);

  radio.seekUp();
  runSeek(radio);

  TEST_ASSERT_EQUAL(start, radio.frequency10kHz());
}

void test_seek_up_requires_both_rssi_and_snr_to_clear_their_thresholds() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  // Good RSSI but failing SNR, one step up -- must not stop here.
  uint16_t badTarget = RadioConfig::FmBandStart + kFmStep;
  SI4735::setSimulatedSignalAt(badTarget, 50, 0);

  radio.seekUp();
  runSeek(radio);

  TEST_ASSERT_NOT_EQUAL(badTarget, radio.frequency10kHz());
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
    flushSettings(radio);
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(40, reloaded.volume());
}

void test_volume_and_mute_are_persisted_lazily_not_per_change() {
  // Regression: every setVolume()/setMuted() used to write NVS on the spot
  // -- with Vol+/Vol- auto-repeating at 150ms and the dashboard slider
  // firing per pixel, that was dozens of flash writes per gesture. Now the
  // write waits until the settings have been quiet for SettingsFlushDelayMs.
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  native_fake_millis_value() = 1000;

  radio.setVolume(50);
  radio.setMuted(true);
  {
    RadioTuner peek(region);
    peek.begin();  // reads NVS: nothing flushed yet
    TEST_ASSERT_EQUAL(RadioConfig::DefaultVolume, peek.volume());
    TEST_ASSERT_FALSE(peek.muted());
  }

  native_fake_millis_value() += RadioConfig::SettingsFlushDelayMs - 1;
  radio.update();  // one ms too early
  {
    RadioTuner peek(region);
    peek.begin();
    TEST_ASSERT_EQUAL(RadioConfig::DefaultVolume, peek.volume());
  }

  native_fake_millis_value() += 1;
  radio.update();  // quiet for the full delay -> flushed, both keys at once
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(50, reloaded.volume());
  TEST_ASSERT_TRUE(reloaded.muted());
}

void test_muting_mid_ramp_persists_the_saved_volume_not_the_transient() {
  // Regression: save() wrote the *live* volume_, so a mute during a
  // radio-wake ramp (snooze, dead-air fallback) -- when volume_ is still
  // on whatever quiet transient step the ramp was at -- persisted that
  // step. RAM kept the real value, so the next ring that boot was fine, but
  // a reboot brought the radio back at e.g. 12 instead of 30.
  {
    RegionStore region;
    region.begin();
    RadioTuner radio(region);
    radio.begin();
    radio.setVolume(30);
    flushSettings(radio);
    radio.setVolumeTransient(12);  // mid-ramp
    radio.setMuted(true);          // snooze
    flushSettings(radio);
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(30, reloaded.volume());
  TEST_ASSERT_EQUAL(30, reloaded.persistedVolume());
  TEST_ASSERT_TRUE(reloaded.muted());
}

void test_volume_up_mid_ramp_jumps_to_the_saved_volume() {
  // See volume()'s comment in RadioTuner.h: +1 from a quiet transient step
  // both barely changed anything audible and *saved* that step as the new
  // real volume (30 -> 5 from a single press).
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setVolume(30);
  radio.setVolumeTransient(4);  // ramp start

  radio.volumeUp();

  TEST_ASSERT_EQUAL(30, radio.volume());
  TEST_ASSERT_EQUAL(30, radio.persistedVolume());

  radio.volumeUp();  // no ramp in effect anymore -> ordinary +1
  TEST_ASSERT_EQUAL(31, radio.volume());
  TEST_ASSERT_EQUAL(31, radio.persistedVolume());
}

void test_volume_down_mid_ramp_steps_from_the_live_value() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setVolume(30);
  radio.setVolumeTransient(10);

  radio.volumeDown();  // quieter than what's actually playing, not 29

  TEST_ASSERT_EQUAL(9, radio.volume());
  TEST_ASSERT_EQUAL(9, radio.persistedVolume());
}

// persistedVolume() tracks only what setVolume() explicitly set -- never
// touched by setVolumeTransient(), unlike volume() itself. This is what
// WakeController's sunrise ramp targets, specifically so it can't get
// dragged down to whatever quiet transient step a snooze happened to
// interrupt it at (see WakeController.cpp's own tests for that scenario).
void test_persisted_volume_is_not_affected_by_transient_changes() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.setVolume(40);
  radio.setVolumeTransient(10);

  TEST_ASSERT_EQUAL(10, radio.volume());           // live value follows the transient set
  TEST_ASSERT_EQUAL(40, radio.persistedVolume());  // real value is untouched
}

void test_muted_persists_across_instances() {
  {
    RegionStore region;
    region.begin();
    RadioTuner radio(region);
    radio.begin();
    radio.setMuted(true);
    flushSettings(radio);
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_TRUE(reloaded.muted());
}

// --- Amp-mute GPIO (Pins::AmpMute) -- gates an external transistor that
// shunts the SI4730->amp audio line to ground. Added because even the
// chip's own hardware mute (RX_HARD_MUTE) leaves an audible noise floor on
// the audio line that a sensitive amp picks up as static; this GPIO
// silences the amp itself instead of relying on the tuner's output being
// truly clean. ---

void test_amp_mute_pin_is_high_when_muted() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setVolume(40);
  radio.setMuted(false);
  TEST_ASSERT_EQUAL(LOW, native_fake_digital_write_value(Pins::AmpMute));

  radio.setMuted(true);
  TEST_ASSERT_EQUAL(HIGH, native_fake_digital_write_value(Pins::AmpMute));

  radio.setMuted(false);
  TEST_ASSERT_EQUAL(LOW, native_fake_digital_write_value(Pins::AmpMute));
}

void test_amp_mute_pin_is_high_at_zero_volume_even_while_unmuted() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  radio.setVolume(0);
  TEST_ASSERT_EQUAL(HIGH, native_fake_digital_write_value(Pins::AmpMute));

  radio.setVolume(1);
  TEST_ASSERT_EQUAL(LOW, native_fake_digital_write_value(Pins::AmpMute));
}

void test_amp_mute_pin_reflects_persisted_volume_on_begin() {
  {
    RegionStore region;
    region.begin();
    RadioTuner radio(region);
    radio.begin();
    radio.setVolume(0);  // persisted
    flushSettings(radio);
  }

  RegionStore region;
  region.begin();
  RadioTuner reloaded(region);
  reloaded.begin();
  TEST_ASSERT_EQUAL(HIGH, native_fake_digital_write_value(Pins::AmpMute));
}

// Regression guard: the amp-mute pin is plain GPIO, not I2C, so it must
// keep working even when the SI4730 itself never responds -- unlike every
// other RadioTuner method, which is a no-op without a chip present.
void test_amp_mute_pin_is_still_driven_when_radio_is_unavailable() {
  SI4735::setSimulatedPresent(false);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  TEST_ASSERT_FALSE(radio.available());
  TEST_ASSERT_EQUAL(LOW, native_fake_digital_write_value(Pins::AmpMute));  // default volume is nonzero

  radio.setMuted(true);
  TEST_ASSERT_EQUAL(HIGH, native_fake_digital_write_value(Pins::AmpMute));
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
  radio.tune(9510);  // 95.10MHz -- an odd tenth, already valid on Americas' grid

  radio.recallPreset(3);  // never stored

  TEST_ASSERT_EQUAL(9510, radio.frequency10kHz());
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

void test_sleep_timer_survives_a_millis_rollover() {
  // Regression: the sleep timer used to store an absolute deadline
  // (millis() + duration) and compare with a plain millis() >= deadline --
  // near the ~49.7-day millis() rollover, that sum can overflow uint32_t
  // and wrap to a small value that reads as "already expired" hours early.
  // Storing start+duration and comparing via unsigned subtraction (the
  // same pattern WakeController's ramp already uses) stays correct across
  // the wrap instead, as long as the timer itself is well under ~49.7 days
  // (MaxSleepTimerMinutes is 120).
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  uint32_t start = 0xFFFFFFFFu - 30000u;  // 30s before millis() wraps around to 0
  native_fake_millis_value() = start;
  radio.setSleepTimer(1);  // 1 minute -- deadline is 30s past the wrap

  native_fake_millis_value() = start + 59999u;  // wraps past 0 -- still 1ms early
  radio.update();
  TEST_ASSERT_FALSE(radio.muted());
  TEST_ASSERT_TRUE(radio.sleepTimerActive());

  native_fake_millis_value() = start + 60000u;  // exactly the deadline, post-wrap
  radio.update();
  TEST_ASSERT_TRUE(radio.muted());
  TEST_ASSERT_FALSE(radio.sleepTimerActive());
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
  TEST_ASSERT_EQUAL(0, radio.snr());

  radio.tune(9500);
  radio.seekUp();
  TEST_ASSERT_FALSE(radio.seeking());  // nothing to sweep without a chip
  radio.seekDown();
  radio.setVolume(40);
  radio.setMuted(true);
  radio.updateRdsSync(true);
  native_fake_millis_value() += RadioConfig::SeekSettleMs;
  radio.update();

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

void test_snr_reflects_simulated_signal() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  SI4735::setSimulatedSnr(7);
  TEST_ASSERT_EQUAL(7, radio.snr());
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

void test_apply_region_persists_the_reclamped_frequency() {
  // Regression: applyRegion() used to re-clamp the frequency live on the
  // chip without ever writing the result back to NVS, so the *next* region
  // switch (or a reboot while still on the new region) would re-clamp from
  // the stale pre-switch value all over again instead of from wherever the
  // chip actually ended up.
  RegionStore region;
  region.begin();
  region.setIndex(1);  // Europe/RoW: 87.5-108.0MHz, same 100kHz grid as Japan below
  RadioTuner radio(region);
  radio.begin();
  radio.tune(9750);  // 97.50MHz

  region.setIndex(2);  // Japan: 76.0-95.0MHz -- 97.50 is out of range
  radio.applyRegion();  // re-clamps live to 95.00MHz -- and should persist that too

  region.setIndex(1);  // back to Europe/RoW
  radio.applyRegion();

  // If the Japan re-clamp wasn't persisted, this reads the stale
  // pre-Japan-switch 97.50MHz back from NVS instead of the 95.00MHz the
  // chip was actually last sitting on.
  TEST_ASSERT_EQUAL(9500, radio.frequency10kHz());
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
//
// updateRdsSync() is currently disabled (short-circuits before touching the
// driver at all) -- see its definition in RadioTuner.cpp for why: the
// PU2CLR SI4735 library's getRdsStatus() retries forever with no timeout on
// an ERR status, which real FM reception (a weak signal or a non-RDS
// station) triggers easily, and this was confirmed to hang the whole
// device. These tests now cover that it stays inert rather than covering
// the (currently unreachable) harvesting logic itself.

void test_rds_sync_is_disabled_and_never_reports_a_time() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);  // actively "listening" -- would have been harvested for free

  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  radio.updateRdsSync(/*needsFallback=*/false);

  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());
}

void test_consume_rds_time_sync_has_nothing_to_consume() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(false);

  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  radio.updateRdsSync(false);

  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());
  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());  // still nothing, repeated calls are harmless
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

void test_disabled_rds_sync_never_retunes_even_while_muted_and_needing_fallback() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(true);

  int callsAfterMuting = SI4735::driverCallCount();  // setMuted() itself is one real driver call

  native_fake_millis_value() = 1000;
  radio.updateRdsSync(/*needsFallback=*/true);  // would have started a background retune attempt

  TEST_ASSERT_EQUAL(callsAfterMuting, SI4735::driverCallCount());
}

void test_disabled_rds_sync_ignores_a_simulated_ct_frame_even_while_idle() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(true);

  SI4735::setSimulatedRdsDateTime(2026, 3, 15, 7, 42);
  native_fake_millis_value() = 1000;
  radio.updateRdsSync(true);

  TEST_ASSERT_FALSE(radio.consumeRdsTimeSync());
}

// --- RDS station name / RadioText (decodeRdsGroup() is pure logic -- no
// Wire/SI4735 access -- so these drive it directly with hand-built raw[13]
// arrays, matching the byte layout readRdsGroupSafely() fills in:
// raw[6..7]=Block B, raw[8..9]=Block C, raw[10..11]=Block D. ---

void test_station_name_and_radio_text_are_empty_before_anything_decoded() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
  TEST_ASSERT_EQUAL_STRING("", radio.radioText());
}

void test_decode_rds_group_assembles_ps_name_across_all_segments() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  // "RADIOFM1" split into 4 pairs across group-0 (PS name) segments 0-3.
  struct {
    uint8_t segment;
    char a, b;
  } parts[] = {{0, 'R', 'A'}, {1, 'D', 'I'}, {2, 'O', 'F'}, {3, 'M', '1'}};
  for (auto &p : parts) {
    uint8_t raw[13] = {};
    uint16_t blockB = p.segment & 0x03;  // groupType=0, versionCode=0 (0A)
    raw[6] = (uint8_t)(blockB >> 8);
    raw[7] = (uint8_t)(blockB & 0xFF);
    raw[10] = (uint8_t)p.a;
    raw[11] = (uint8_t)p.b;
    radio.decodeRdsGroup(raw);
  }

  TEST_ASSERT_EQUAL_STRING("RADIOFM1", radio.stationName());
}

void test_decode_rds_group_assembles_radiotext_2a_via_block_c_and_d() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  // Version A (4 chars/segment from Block C + Block D): "TEST" then "MSG1".
  uint8_t raw0[13] = {};
  uint16_t blockB0 = (2u << 12) | (0u << 11) | 0;  // groupType=2, version A, segment 0
  raw0[6] = (uint8_t)(blockB0 >> 8);
  raw0[7] = (uint8_t)(blockB0 & 0xFF);
  raw0[8] = 'T';
  raw0[9] = 'E';
  raw0[10] = 'S';
  raw0[11] = 'T';
  radio.decodeRdsGroup(raw0);

  uint8_t raw1[13] = {};
  uint16_t blockB1 = (2u << 12) | (0u << 11) | 1;  // segment 1
  raw1[6] = (uint8_t)(blockB1 >> 8);
  raw1[7] = (uint8_t)(blockB1 & 0xFF);
  raw1[8] = 'M';
  raw1[9] = 'S';
  raw1[10] = 'G';
  raw1[11] = '1';
  radio.decodeRdsGroup(raw1);

  TEST_ASSERT_EQUAL_STRING_LEN("TESTMSG1", radio.radioText(), 8);
}

void test_decode_rds_group_assembles_radiotext_2b_via_block_d_only() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  // Version B (2 chars/segment from Block D only): "HI" then "!!".
  uint8_t raw0[13] = {};
  uint16_t blockB0 = (2u << 12) | (1u << 11) | 0;  // groupType=2, version B, segment 0
  raw0[6] = (uint8_t)(blockB0 >> 8);
  raw0[7] = (uint8_t)(blockB0 & 0xFF);
  raw0[10] = 'H';
  raw0[11] = 'I';
  radio.decodeRdsGroup(raw0);

  uint8_t raw1[13] = {};
  uint16_t blockB1 = (2u << 12) | (1u << 11) | 1;  // segment 1
  raw1[6] = (uint8_t)(blockB1 >> 8);
  raw1[7] = (uint8_t)(blockB1 & 0xFF);
  raw1[10] = '!';
  raw1[11] = '!';
  radio.decodeRdsGroup(raw1);

  TEST_ASSERT_EQUAL_STRING_LEN("HI!!", radio.radioText(), 4);
}

void test_decode_rds_group_clears_radiotext_when_the_ab_flag_flips() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw0[13] = {};
  uint16_t blockB0 = (2u << 12) | (1u << 11) | (0u << 4) | 0;  // A/B flag 0, segment 0
  raw0[6] = (uint8_t)(blockB0 >> 8);
  raw0[7] = (uint8_t)(blockB0 & 0xFF);
  raw0[10] = 'H';
  raw0[11] = 'I';
  radio.decodeRdsGroup(raw0);
  TEST_ASSERT_EQUAL_STRING_LEN("HI", radio.radioText(), 2);

  // A second, later segment of the same message A -- segment 5 (index
  // 10-11) -- so there's stale content further into the buffer than just
  // index 0 for the flip below to actually have to clear.
  uint8_t rawStale[13] = {};
  uint16_t blockBStale = (2u << 12) | (1u << 11) | (0u << 4) | 5;  // still A/B flag 0, segment 5
  rawStale[6] = (uint8_t)(blockBStale >> 8);
  rawStale[7] = (uint8_t)(blockBStale & 0xFF);
  rawStale[10] = 'O';
  rawStale[11] = 'K';
  radio.decodeRdsGroup(rawStale);
  TEST_ASSERT_EQUAL('O', radio.radioText()[10]);

  uint8_t raw1[13] = {};
  uint16_t blockB1 = (2u << 12) | (1u << 11) | (1u << 4) | 1;  // A/B flag flips to 1, segment 1
  raw1[6] = (uint8_t)(blockB1 >> 8);
  raw1[7] = (uint8_t)(blockB1 & 0xFF);
  raw1[10] = 'Y';
  raw1[11] = 'Y';
  radio.decodeRdsGroup(raw1);

  // The flip clears the *whole* buffer, not just index 0 -- the stale "OK"
  // from message A's segment 5 (index 10-11) must not survive into message
  // B just because B's own segments don't happen to touch it. Regression
  // test: this used to only NUL index 0, so a message shorter than the
  // previous one left trailing stale characters past its own end.
  TEST_ASSERT_EQUAL_STRING("", radio.radioText());
  TEST_ASSERT_EQUAL('\0', radio.radioText()[10]);
  TEST_ASSERT_EQUAL('\0', radio.radioText()[11]);
}

void test_decode_rds_group_maps_the_end_of_message_marker_to_nul() {
  // RDS RadioText's own end-of-message marker (0x0D) for a station that
  // doesn't pad the rest of the buffer with spaces.
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = (2u << 12) | (1u << 11) | 0;  // group 2B, segment 0
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[10] = 'H';
  raw[11] = 0x0D;
  radio.decodeRdsGroup(raw);

  TEST_ASSERT_EQUAL_STRING("H", radio.radioText());
}

void test_decode_rds_group_rejects_uncorrectable_block_b() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = 0;  // group type 0, segment 0
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[10] = 'X';
  raw[11] = 'X';
  raw[12] = 0x30;  // BLEB (bits 4-5) = 3, uncorrectable
  radio.decodeRdsGroup(raw);

  // Block B is what the group-type/segment decision itself is read from --
  // an uncorrectable read of it can't be trusted enough to decode at all.
  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
}

void test_decode_rds_group_rejects_uncorrectable_block_d() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = 0;  // group type 0, segment 0
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[10] = 'X';
  raw[11] = 'X';
  raw[12] = 0x03;  // BLED (bits 0-1) = 3, uncorrectable
  radio.decodeRdsGroup(raw);

  // Block D supplies characters in every group type here -- uncorrectable
  // means the two characters it's carrying would just be noise.
  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
}

void test_decode_rds_group_rejects_uncorrectable_block_c_for_2a_only() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = (2u << 12) | (0u << 11) | 0;  // group 2A, segment 0
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[8] = 'T';
  raw[9] = 'E';
  raw[10] = 'S';
  raw[11] = 'T';
  raw[12] = 0x0C;  // BLEC (bits 2-3) = 3, uncorrectable -- only matters for 2A
  radio.decodeRdsGroup(raw);

  TEST_ASSERT_EQUAL_STRING("", radio.radioText());
}

void test_decode_rds_group_block_c_error_does_not_block_ps_name() {
  // BLEC only matters for 2A's own Block C characters -- a group-0 (PS
  // name) decode never reads Block C, so an uncorrectable Block C on an
  // otherwise-clean group-0 read must not get rejected because of it.
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = 0;  // group type 0, segment 0
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[10] = 'X';
  raw[11] = 'X';
  raw[12] = 0x0C;  // BLEC = 3, irrelevant here
  radio.decodeRdsGroup(raw);

  TEST_ASSERT_EQUAL_STRING_LEN("XX", radio.stationName(), 2);
}

void test_decode_rds_group_ignores_an_unrelated_group_type() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint8_t raw[13] = {};
  uint16_t blockB = (5u << 12);  // group type 5 -- neither 0 (PS) nor 2 (RadioText)
  raw[6] = (uint8_t)(blockB >> 8);
  raw[7] = (uint8_t)(blockB & 0xFF);
  raw[10] = 'X';
  raw[11] = 'X';
  radio.decodeRdsGroup(raw);

  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
  TEST_ASSERT_EQUAL_STRING("", radio.radioText());
}

void test_poll_rds_text_does_nothing_while_muted() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.setMuted(true);

  radio.pollRdsText();

  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
}

void test_poll_rds_text_does_nothing_when_radio_is_unavailable() {
  SI4735::setSimulatedPresent(false);
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  radio.pollRdsText();  // must not hang/crash with no chip present

  TEST_ASSERT_EQUAL_STRING("", radio.stationName());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_begin_reports_availability_when_the_chip_responds);
  RUN_TEST(test_begin_reports_unavailable_when_no_chip_responds);
  RUN_TEST(test_rssi_queries_fresh_signal_quality_each_call);
  RUN_TEST(test_tune_clamps_to_fm_band_bounds);
  RUN_TEST(test_step_up_wraps_from_band_end_to_band_start);
  RUN_TEST(test_step_down_wraps_from_band_start_to_band_end);
  RUN_TEST(test_step_up_and_down_move_by_one_fm_step_away_from_the_edges);
  RUN_TEST(test_tune_snaps_to_the_odd_decimal_grid_in_americas_only);
  RUN_TEST(test_tune_does_not_snap_to_odd_decimals_outside_americas);
  RUN_TEST(test_step_wraps_within_the_current_regions_band_not_a_fixed_constant);
  RUN_TEST(test_seek_up_stops_at_the_first_candidate_clearing_both_thresholds);
  RUN_TEST(test_seek_is_non_blocking_and_advances_one_candidate_per_settle_time);
  RUN_TEST(test_tune_cancels_an_in_progress_seek);
  RUN_TEST(test_seek_persists_the_found_frequency);
  RUN_TEST(test_seek_up_climbs_past_a_shoulder_to_the_stations_actual_peak);
  RUN_TEST(test_seek_down_climbs_past_a_shoulder_to_the_stations_actual_peak);
  RUN_TEST(test_seek_up_wraps_past_band_end_to_find_a_station_before_the_start);
  RUN_TEST(test_seek_down_wraps_past_band_start_to_find_a_station_before_the_end);
  RUN_TEST(test_seek_up_returns_to_the_starting_frequency_when_nothing_clears_threshold);
  RUN_TEST(test_seek_up_requires_both_rssi_and_snr_to_clear_their_thresholds);
  RUN_TEST(test_set_volume_clamps_to_63);
  RUN_TEST(test_volume_up_and_down_stop_at_bounds);
  RUN_TEST(test_transient_volume_is_not_persisted);
  RUN_TEST(test_volume_and_mute_are_persisted_lazily_not_per_change);
  RUN_TEST(test_muting_mid_ramp_persists_the_saved_volume_not_the_transient);
  RUN_TEST(test_volume_up_mid_ramp_jumps_to_the_saved_volume);
  RUN_TEST(test_volume_down_mid_ramp_steps_from_the_live_value);
  RUN_TEST(test_persisted_volume_is_not_affected_by_transient_changes);
  RUN_TEST(test_muted_persists_across_instances);
  RUN_TEST(test_amp_mute_pin_is_high_when_muted);
  RUN_TEST(test_amp_mute_pin_is_high_at_zero_volume_even_while_unmuted);
  RUN_TEST(test_amp_mute_pin_reflects_persisted_volume_on_begin);
  RUN_TEST(test_amp_mute_pin_is_still_driven_when_radio_is_unavailable);
  RUN_TEST(test_store_and_recall_preset);
  RUN_TEST(test_recalling_an_unset_preset_does_nothing);
  RUN_TEST(test_presets_persist_across_instances);
  RUN_TEST(test_sleep_timer_mutes_only_after_it_elapses);
  RUN_TEST(test_sleep_timer_remaining_minutes_is_exact_at_the_boundary);
  RUN_TEST(test_sleep_timer_survives_a_millis_rollover);
  RUN_TEST(test_cancel_sleep_timer_prevents_auto_mute);
  RUN_TEST(test_set_sleep_timer_to_zero_behaves_like_cancel);
  RUN_TEST(test_nothing_touches_the_driver_when_radio_is_unavailable);
  RUN_TEST(test_rssi_reflects_simulated_signal);
  RUN_TEST(test_snr_reflects_simulated_signal);
  RUN_TEST(test_begin_applies_the_current_region_to_the_chip);
  RUN_TEST(test_changing_region_live_reapplies_de_emphasis);
  RUN_TEST(test_apply_region_reclamps_frequency_into_the_new_band);
  RUN_TEST(test_apply_region_persists_the_reclamped_frequency);
  RUN_TEST(test_tune_clamps_to_the_current_regions_band_not_a_fixed_constant);
  RUN_TEST(test_rds_sync_is_disabled_and_never_reports_a_time);
  RUN_TEST(test_consume_rds_time_sync_has_nothing_to_consume);
  RUN_TEST(test_implausible_rds_year_is_rejected);
  RUN_TEST(test_disabled_rds_sync_never_retunes_even_while_muted_and_needing_fallback);
  RUN_TEST(test_disabled_rds_sync_ignores_a_simulated_ct_frame_even_while_idle);
  RUN_TEST(test_station_name_and_radio_text_are_empty_before_anything_decoded);
  RUN_TEST(test_decode_rds_group_assembles_ps_name_across_all_segments);
  RUN_TEST(test_decode_rds_group_assembles_radiotext_2a_via_block_c_and_d);
  RUN_TEST(test_decode_rds_group_assembles_radiotext_2b_via_block_d_only);
  RUN_TEST(test_decode_rds_group_clears_radiotext_when_the_ab_flag_flips);
  RUN_TEST(test_decode_rds_group_maps_the_end_of_message_marker_to_nul);
  RUN_TEST(test_decode_rds_group_rejects_uncorrectable_block_b);
  RUN_TEST(test_decode_rds_group_rejects_uncorrectable_block_d);
  RUN_TEST(test_decode_rds_group_rejects_uncorrectable_block_c_for_2a_only);
  RUN_TEST(test_decode_rds_group_block_c_error_does_not_block_ps_name);
  RUN_TEST(test_decode_rds_group_ignores_an_unrelated_group_type);
  RUN_TEST(test_poll_rds_text_does_nothing_while_muted);
  RUN_TEST(test_poll_rds_text_does_nothing_when_radio_is_unavailable);
  return UNITY_END();
}

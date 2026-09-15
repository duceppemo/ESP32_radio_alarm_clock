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
  TEST_ASSERT_EQUAL(RadioConfig::FmBandEnd, radio.frequency10kHz());
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

  TEST_ASSERT_EQUAL(RadioConfig::FmBandEnd, radio.frequency10kHz());
}

void test_step_up_and_down_move_by_one_fm_step_away_from_the_edges() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();

  uint16_t mid = (RadioConfig::FmBandStart + RadioConfig::FmBandEnd) / 2;
  radio.tune(mid);

  radio.stepUp();
  TEST_ASSERT_EQUAL(mid + RadioConfig::FmStep, radio.frequency10kHz());

  radio.stepDown();
  radio.stepDown();
  TEST_ASSERT_EQUAL(mid - RadioConfig::FmStep, radio.frequency10kHz());
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
  uint16_t target = RadioConfig::FmBandStart + 5 * RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(target, RadioConfig::SeekRssiThreshold, RadioConfig::SeekSnrThreshold);

  radio.seekUp();

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
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
  uint16_t shoulder = RadioConfig::FmBandStart + 5 * RadioConfig::FmStep;
  uint16_t peak = shoulder + RadioConfig::FmStep;
  uint16_t farSide = peak + RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(shoulder, 24, 8);
  SI4735::setSimulatedSignalAt(peak, 31, 16);
  SI4735::setSimulatedSignalAt(farSide, 22, 6);  // falling off again past the peak

  radio.seekUp();

  TEST_ASSERT_EQUAL(peak, radio.frequency10kHz());
}

void test_seek_down_climbs_past_a_shoulder_to_the_stations_actual_peak() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandEnd);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t shoulder = RadioConfig::FmBandEnd - 5 * RadioConfig::FmStep;
  uint16_t peak = shoulder - RadioConfig::FmStep;
  uint16_t farSide = peak - RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(shoulder, 24, 8);
  SI4735::setSimulatedSignalAt(peak, 31, 16);
  SI4735::setSimulatedSignalAt(farSide, 22, 6);

  radio.seekDown();

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
  radio.tune(RadioConfig::FmBandEnd - RadioConfig::FmStep);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = RadioConfig::FmBandStart + 2 * RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  radio.seekUp();

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_seek_down_wraps_past_band_start_to_find_a_station_before_the_end() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  radio.tune(RadioConfig::FmBandStart + RadioConfig::FmStep);
  SI4735::setSimulatedRssi(0);
  SI4735::setSimulatedSnr(0);
  uint16_t target = RadioConfig::FmBandEnd - 2 * RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(target, 50, 20);

  radio.seekDown();

  TEST_ASSERT_EQUAL(target, radio.frequency10kHz());
}

void test_seek_up_returns_to_the_starting_frequency_when_nothing_clears_threshold() {
  RegionStore region;
  region.begin();
  RadioTuner radio(region);
  radio.begin();
  uint16_t start = RadioConfig::FmBandStart + 10 * RadioConfig::FmStep;
  radio.tune(start);
  SI4735::setSimulatedRssi(0);  // dead air across the entire band, no exceptions
  SI4735::setSimulatedSnr(0);

  radio.seekUp();

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
  uint16_t badTarget = RadioConfig::FmBandStart + RadioConfig::FmStep;
  SI4735::setSimulatedSignalAt(badTarget, 50, 0);

  radio.seekUp();

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
  TEST_ASSERT_EQUAL(0, radio.snr());

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

  uint8_t raw1[13] = {};
  uint16_t blockB1 = (2u << 12) | (1u << 11) | (1u << 4) | 1;  // A/B flag flips to 1, segment 1
  raw1[6] = (uint8_t)(blockB1 >> 8);
  raw1[7] = (uint8_t)(blockB1 & 0xFF);
  raw1[10] = 'Y';
  raw1[11] = 'Y';
  radio.decodeRdsGroup(raw1);

  // The flip clears the message (index 0 goes back to NUL) even though the
  // new segment's bytes land later in the buffer -- a fresh message starting
  // mid-buffer must not appear to continue the old one.
  TEST_ASSERT_EQUAL_STRING("", radio.radioText());
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
  RUN_TEST(test_step_wraps_within_the_current_regions_band_not_a_fixed_constant);
  RUN_TEST(test_seek_up_stops_at_the_first_candidate_clearing_both_thresholds);
  RUN_TEST(test_seek_up_climbs_past_a_shoulder_to_the_stations_actual_peak);
  RUN_TEST(test_seek_down_climbs_past_a_shoulder_to_the_stations_actual_peak);
  RUN_TEST(test_seek_up_wraps_past_band_end_to_find_a_station_before_the_start);
  RUN_TEST(test_seek_down_wraps_past_band_start_to_find_a_station_before_the_end);
  RUN_TEST(test_seek_up_returns_to_the_starting_frequency_when_nothing_clears_threshold);
  RUN_TEST(test_seek_up_requires_both_rssi_and_snr_to_clear_their_thresholds);
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
  RUN_TEST(test_snr_reflects_simulated_signal);
  RUN_TEST(test_begin_applies_the_current_region_to_the_chip);
  RUN_TEST(test_changing_region_live_reapplies_de_emphasis);
  RUN_TEST(test_apply_region_reclamps_frequency_into_the_new_band);
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
  RUN_TEST(test_decode_rds_group_ignores_an_unrelated_group_type);
  RUN_TEST(test_poll_rds_text_does_nothing_while_muted);
  RUN_TEST(test_poll_rds_text_does_nothing_when_radio_is_unavailable);
  return UNITY_END();
}

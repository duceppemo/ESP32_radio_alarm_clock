#pragma once

#include <Arduino.h>

// Bump manually on every release-worthy change (no build-time git/CI wiring
// for this yet). Shown on the TFT boot screen (main.cpp) so a given unit's
// running build is identifiable at a glance without a serial connection.
constexpr const char *FirmwareVersion = "0.1.0";

// ---------------------------------------------------------------------------
// Pin assignments off the shared I2C bus (see docs/wiring-diagram.html).
// Confirmed on real hardware: RadioReset, SnoozeButton, VolumeUp, VolumeDown
// (all wired and working as assigned below). Buzzer is still an unconfirmed
// placeholder -- not yet wired/tested.
//
// Two independent, non-overlapping audio paths:
//   - FM/AM playback: SI4730 (analog audio out) -> amp -> speaker. Pure
//     analog, wired straight to each other -- the ESP32 is never in this
//     signal path, only on I2C to send tune/volume commands to the SI4730.
//   - Alarm tones (AlarmSound): ESP32 -> piezo buzzer on Pins::Buzzer via
//     Arduino's tone()/noTone(), unrelated to the amp/speaker entirely.
// ---------------------------------------------------------------------------
namespace Pins {
constexpr uint8_t RadioReset = A0;
constexpr uint8_t SnoozeButton = A1;
constexpr uint8_t VolumeUp = A2;
constexpr uint8_t VolumeDown = A3;
constexpr uint8_t Buzzer = A5;  // unconfirmed -- not yet wired/tested

// Onboard menu buttons (Adafruit ESP32-S3 Reverse TFT Feather pinout) --
// confirmed working on real hardware, including D1/D2's INPUT_PULLDOWN
// requirement (see MenuSystem::begin()).
constexpr uint8_t MenuSelect = 0;  // D0, shares the boot-strap pin
constexpr uint8_t MenuUp = 1;      // D1
constexpr uint8_t MenuDown = 2;    // D2
}  // namespace Pins

namespace AlarmConfig {
constexpr uint8_t MaxAlarms = 3;
constexpr uint8_t DefaultSnoozeMinutes = 9;
constexpr uint8_t MinSnoozeMinutes = 1;
constexpr uint8_t MaxSnoozeMinutes = 60;

// Gradual/"sunrise" wake: volume ramps from WakeRampStartVolume up to
// whatever volume was last set, over WakeRampSeconds. Was 90s -- live
// testing found that dragged on too long at the quiet starting volume
// before becoming clearly audible, so shortened to 15s; that turned out
// too abrupt in turn, so settled on 30s.
constexpr uint16_t WakeRampSeconds = 30;
constexpr uint8_t WakeRampStartVolume = 4;

// If waking via radio, how long to let it ramp before checking for a
// station; below this RSSI it's treated as dead air and AlarmSound takes
// over instead. RSSI scale/threshold are unverified without real hardware.
constexpr uint16_t DeadAirCheckDelaySeconds = 5;
constexpr uint8_t DeadAirRssiThreshold = 10;
}  // namespace AlarmConfig

namespace RadioConfig {
// Default (Americas/Europe -- they share the same FM band) tuning range;
// Japan's is genuinely different (76.0-95.0MHz) and lives in RegionStore's
// own table instead, which RadioTuner reads from rather than these
// constants once a chip is present. Still the fallback used before a
// region is known, and the band RegionStore's Americas/Europe entries
// reference.
constexpr uint16_t FmBandStart = 8750;   // 87.50 MHz, in 10 kHz units
constexpr uint16_t FmBandEnd = 10800;    // 108.00 MHz
constexpr uint16_t FmStep = 10;          // 100 kHz steps
constexpr uint16_t FmDefaultFreq = 9750; // 97.50 MHz

// Minimum RSSI (dBuV) / SNR (dB) RadioTuner's own software seek (see
// seekUp()/seekDown()) requires before stopping on a frequency. Measured on
// real hardware, with the Radio screen's live Sig/SNR readout, across a
// dozen stations subjectively "decent quality": Sig ranged 16-29, SNR
// ranged 3-10. RSSI=5 is already well below that Sig floor (not the
// bottleneck); SNR=3 matches the measured floor exactly. Still worth
// revisiting if reception changes (different antenna, different region's
// stations, weaker basement-type reception).
//
// This used to be the SI4735's own hardware SEEK_START command (with these
// same two values pushed to it via setSeekFmRssiThreshold()/
// setSeekFmSNRThreshold()), but that measures signal quality mid-sweep,
// while racing across candidate frequencies -- confirmed live to skip real,
// comfortably-clearing-threshold stations (23 RSSI / 12 SNR once settled)
// in marginal reception, where a fast in-sweep reading is less reliable
// than a settled one. RadioTuner now steps through the band itself,
// settling briefly at each candidate before reading -- slower (a few
// seconds per seek) but consistent with whatever the Radio screen's Sig/SNR
// line already shows for that frequency.
constexpr uint16_t SeekRssiThreshold = 5;
constexpr uint16_t SeekSnrThreshold = 3;
// How long to let RSSI/SNR settle after tuning to each candidate frequency
// during a software seek, before trusting the reading -- a guess (common
// in community SI4735 seek implementations), not measured against real
// AGC/AFC settle time on this specific board.
constexpr uint16_t SeekSettleMs = 30;

// Bounded stand-ins for the PU2CLR SI4735 library's own waitToSend()/
// getRdsStatus() retry timing (300us/poll, matching the library's
// MIN_DELAY_WAIT_SEND_LOOP) -- capped rather than unbounded, since an
// unbounded version of this exact call is what froze the whole device once
// already (see RadioTuner::readRdsGroupSafely()). Guesses at reasonable
// ceilings, not measured against real broadcast timing.
constexpr uint16_t RdsCtsPollDelayUs = 300;
constexpr uint8_t RdsMaxCtsPolls = 50;  // ~15ms worst case
constexpr uint8_t RdsMaxErrRetries = 3;

constexpr uint8_t DefaultVolume = 30;    // SI4735 volume range is 0-63
constexpr uint8_t MaxPresets = 6;
constexpr uint16_t MaxSleepTimerMinutes = 120;

// The snooze button doubles as a sleep-timer toggle when pressed while no
// alarm is ringing and the radio is on -- see SnoozeController.
constexpr uint16_t DefaultSleepTimerMinutes = 30;

// RDS Clock Time fallback sync (see RadioTuner::updateRdsSync()) -- only
// runs while the radio is muted/idle and NTP hasn't synced. Interval/window
// are unverified against real broadcast RDS timing (same caveat as
// DeadAirRssiThreshold below): CT groups aren't guaranteed to repeat often,
// so the window errs generous since this only ever runs in the background.
constexpr uint32_t RdsFallbackIntervalMs = 30UL * 60 * 1000;  // 30 min between attempts
constexpr uint32_t RdsFallbackWindowMs = 60UL * 1000;         // listen up to 60s per attempt

// Plausibility bound on a decoded RDS CT year -- the library's own
// getRdsDateTime() already rejects bad hour/minute/day/month, but not an
// implausible year, which a noisy MJD decode could still produce.
constexpr uint16_t MinPlausibleRdsYear = 2024;
constexpr uint16_t MaxPlausibleRdsYear = 2099;  // matches the DS3231's ~2000-2099 range
}  // namespace RadioConfig

namespace NetConfig {
constexpr const char *ApSsid = "AlarmClock-Setup";
constexpr const char *MdnsHostname = "alarmclock";
constexpr uint32_t StaConnectTimeoutMs = 15000;

// NTP keeps the DS3231 accurate. Timezone (including DST rule, where
// applicable) is a user setting -- see TimezoneStore -- not hardcoded here.
constexpr const char *NtpServer = "pool.ntp.org";
constexpr uint32_t NtpResyncIntervalMs = 24UL * 60 * 60 * 1000;

// Dashboard/OTA login. The username defaults to this constant, but the
// password is never a fixed value baked into every unit -- see
// WebDashboard::loadOrCreateAdminCredentials(), which generates a random
// per-device default on first boot and persists it, changeable later from
// the dashboard's Security section.
constexpr const char *DefaultAdminUsername = "admin";
}  // namespace NetConfig

namespace BatteryConfig {
constexpr uint8_t LowPercentThreshold = 15;
}  // namespace BatteryConfig

namespace DisplayConfig {
// Ambient-light thresholds (VEML7700 lux reading) the auto-dim curve is
// linear between -- at/below Dim, displays sit at their minimum; at/above
// Bright, full brightness. Unverified against a real room; expect to retune
// once hardware exists, same as the dead-air RSSI threshold.
constexpr float DimLuxThreshold = 5.0f;
constexpr float BrightLuxThreshold = 200.0f;

// TFT backlight is PWM-driven (0-255); never fully off so the clock stays
// readable in a dark room. 7-segment brightness is the HT16K33's native
// 0-15 range -- 0 is its dimmest setting (still lit, just at minimum duty
// cycle), used here as the floor since the 7-segment's own LEDs are bright
// enough at even the lowest setting to want going as dim as the chip allows
// in a dark room, unlike the TFT backlight above.
constexpr uint8_t MinTftBacklight = 20;
constexpr uint8_t MaxTftBacklight = 255;
constexpr uint8_t MinSevenSegmentBrightness = 0;
constexpr uint8_t MaxSevenSegmentBrightness = 15;
}  // namespace DisplayConfig

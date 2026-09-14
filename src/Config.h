#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin assignments off the shared I2C bus. None of this hardware is wired up
// yet (see docs/wiring-diagram.html) -- these are placeholders on currently
// free broken-out pins of the ESP32-S3 Reverse TFT Feather and MUST be
// confirmed once the panel controls and radio module are actually wired.
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
constexpr uint8_t Buzzer = A5;

// Onboard menu buttons (Adafruit ESP32-S3 Reverse TFT Feather pinout).
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
// whatever volume was last set, over WakeRampSeconds.
constexpr uint16_t WakeRampSeconds = 90;
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
// 0-15 range.
constexpr uint8_t MinTftBacklight = 20;
constexpr uint8_t MaxTftBacklight = 255;
constexpr uint8_t MinSevenSegmentBrightness = 1;
constexpr uint8_t MaxSevenSegmentBrightness = 15;
}  // namespace DisplayConfig

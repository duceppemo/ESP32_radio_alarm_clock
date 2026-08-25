#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin assignments off the shared I2C bus. None of this hardware is wired up
// yet (see docs/wiring-diagram.html) -- these are placeholders on currently
// free broken-out pins of the ESP32-S3 Reverse TFT Feather and MUST be
// confirmed once the panel controls and radio module are actually wired.
// ---------------------------------------------------------------------------
namespace Pins {
constexpr uint8_t RadioReset = A0;
constexpr uint8_t SnoozeButton = A1;
constexpr uint8_t EncoderA = A2;
constexpr uint8_t EncoderB = A3;
constexpr uint8_t EncoderSwitch = A4;
constexpr uint8_t AmpI2sBclk = A5;
constexpr uint8_t AmpI2sLrc = RX;
constexpr uint8_t AmpI2sDin = TX;

// Onboard menu buttons (Adafruit ESP32-S3 Reverse TFT Feather pinout).
constexpr uint8_t MenuSelect = 0;  // D0, shares the boot-strap pin
constexpr uint8_t MenuUp = 1;      // D1
constexpr uint8_t MenuDown = 2;    // D2
}  // namespace Pins

namespace AlarmConfig {
constexpr uint8_t MaxAlarms = 3;
constexpr uint8_t DefaultSnoozeMinutes = 9;
}  // namespace AlarmConfig

namespace RadioConfig {
constexpr uint16_t FmBandStart = 8750;   // 87.50 MHz, in 10 kHz units
constexpr uint16_t FmBandEnd = 10800;    // 108.00 MHz
constexpr uint16_t FmStep = 10;          // 100 kHz steps
constexpr uint16_t FmDefaultFreq = 9750; // 97.50 MHz
constexpr uint8_t DefaultVolume = 30;    // SI4735 volume range is 0-63
constexpr uint8_t MaxPresets = 6;
}  // namespace RadioConfig

namespace NetConfig {
constexpr const char *ApSsid = "AlarmClock-Setup";
constexpr const char *MdnsHostname = "alarmclock";
constexpr uint32_t StaConnectTimeoutMs = 15000;
}  // namespace NetConfig

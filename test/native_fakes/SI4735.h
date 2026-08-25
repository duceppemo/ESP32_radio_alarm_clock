#pragma once

#include <cstdint>

#define FM_CURRENT_MODE 0
#define AM_CURRENT_MODE 1

// Minimal native stand-in for the PU2CLR SI4735 driver -- just enough of
// the API RadioTuner calls to test its own wrapper logic (band clamping,
// presets, sleep timer, transient-vs-persisted volume) without real I2C
// hardware. Records what was set and lets tests control what "the chip"
// reports back.
//
// getCurrentRSSI() reads a process-wide simulated value rather than
// per-instance state, since tests only ever have one RadioTuner alive at a
// time and this avoids needing a test-only accessor into RadioTuner just to
// reach its private SI4735 member.
class SI4735 {
 public:
  void setup(uint8_t resetPin, uint8_t defaultFunction) {
    (void)resetPin;
    (void)defaultFunction;
  }
  void setFM(uint16_t fromFreq, uint16_t toFreq, uint16_t initialFreq, uint16_t step) {
    (void)fromFreq;
    (void)toFreq;
    (void)step;
    frequency = initialFreq;
  }
  void setFrequency(uint16_t freq) { frequency = freq; }
  uint16_t getFrequency() { return frequency; }

  void setVolume(uint8_t v) { volume = v; }

  void seekStationUp() { seekUpCalls++; }
  void seekStationDown() { seekDownCalls++; }

  uint8_t getCurrentRSSI() { return simulatedRssi(); }
  void setAudioMute(bool m) { muted = m; }

  static void setSimulatedRssi(uint8_t value) { simulatedRssi() = value; }
  static void resetSimulatedRssi() { simulatedRssi() = 50; }  // default: "good signal"

  // Test-observable state.
  uint16_t frequency = 0;
  uint8_t volume = 0;
  bool muted = false;
  int seekUpCalls = 0;
  int seekDownCalls = 0;

 private:
  static uint8_t &simulatedRssi() {
    static uint8_t v = 50;
    return v;
  }
};

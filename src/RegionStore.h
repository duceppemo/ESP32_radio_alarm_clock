#pragma once

#include <Arduino.h>

// One selectable tuner region. FM fields are wired up to the actual SI4735
// calls (see RadioTuner::applyRegion()); AM fields are stored for when AM
// support gets added later -- this firmware is FM-only today, so they're
// unused for now.
struct RegionEntry {
  const char *label;
  uint8_t fmDeEmphasis;  // SI4735 setFMDeEmphasis(): 1 = 50us, 2 = 75us
  uint16_t fmBandStart;  // 10kHz units, e.g. 8750 = 87.50 MHz
  uint16_t fmBandEnd;    // 10kHz units
  uint8_t amDeEmphasis;  // reserved for future AM support
  uint16_t amStep;       // kHz units, reserved for future AM support
};

// User-selectable tuner region, persisted to NVS -- same shape as
// TimezoneStore (index into a small curated list). Defaults to Americas
// (index 0) on first boot.
class RegionStore {
 public:
  void begin();

  uint8_t index() const { return index_; }
  void setIndex(uint8_t index);  // clamped to [0, count()), persisted
  const RegionEntry &current() const;

  static uint8_t count();
  static const RegionEntry &entry(uint8_t index);

 private:
  void save();
  void load();

  uint8_t index_ = 0;
};

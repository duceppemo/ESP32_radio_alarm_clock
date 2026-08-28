#pragma once

#include <Arduino.h>

// User-selectable clock display format, persisted to NVS. Purely a display
// preference -- the RTC itself always stores/returns 24-hour time; this only
// controls how MenuSystem (TFT) and main.cpp (7-segment) render it. Defaults
// to 24-hour, matching the format used everywhere before this setting
// existed.
class TimeFormatStore {
 public:
  void begin();

  bool is24Hour() const { return is24Hour_; }
  void toggle();  // persisted

 private:
  void save();
  void load();

  bool is24Hour_ = true;
};

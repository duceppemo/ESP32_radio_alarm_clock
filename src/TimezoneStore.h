#pragma once

#include <Arduino.h>

// One selectable timezone entry. posix is a POSIX TZ string (what
// configTzTime() needs) -- these encode both the UTC offset and, where
// applicable, the DST transition rule, so NTP-sourced time converts to the
// correct local time automatically, DST included.
struct TimezoneEntry {
  const char *label;  // shown on the TFT/dashboard
  const char *posix;
};

// User-selectable timezone, persisted to NVS -- replaces what used to be a
// single hardcoded POSIX TZ string in Config.h. Selection is by index into
// a small curated list (index-based cycling suits the TFT's 3-button
// up/down UI as well as a dashboard dropdown); WebDashboard reads
// posixString() when syncing from NTP. Defaults to UTC -- the one entry
// that's correct regardless of where the device actually is, since nothing
// here should guess the user's location.
class TimezoneStore {
 public:
  void begin();

  uint8_t index() const { return index_; }
  void setIndex(uint8_t index);  // clamped to [0, count()), persisted
  void next();
  void previous();

  const char *label() const;
  const char *posixString() const;

  static uint8_t count();
  static const TimezoneEntry &entry(uint8_t index);

 private:
  void save();
  void load();

  uint8_t index_ = 0;
};

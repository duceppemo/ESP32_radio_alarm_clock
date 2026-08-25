#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "Config.h"

struct Alarm {
  uint8_t hour = 7;
  uint8_t minute = 0;
  // Bit i set = active on day i, where 0 = Sunday .. 6 = Saturday
  // (matches RTClib's DateTime::dayOfTheWeek()). Defaults to weekdays.
  uint8_t daysMask = 0b0111110;
  bool enabled = false;

  bool activeOn(uint8_t dayOfWeek) const { return daysMask & (1 << dayOfWeek); }
};

enum class AlarmState { Idle, Ringing, Snoozed };

// Owns the alarm schedule and the ringing/snooze state machine. Persists to
// NVS via Preferences. Hardware-independent: only needs a current DateTime,
// so it works standalone before the RTC is wired up (caller just won't tick
// it) and is easy to unit-drive from tests later.
class AlarmClock {
 public:
  void begin();

  const Alarm &alarm(uint8_t index) const { return alarms_[index]; }
  void setAlarm(uint8_t index, const Alarm &alarm);
  static constexpr uint8_t count() { return AlarmConfig::MaxAlarms; }

  uint8_t snoozeMinutes() const { return snoozeMinutes_; }
  void setSnoozeMinutes(uint8_t minutes);

  // Call once per loop iteration (or at least once per minute) with the
  // current time to evaluate schedules and expire snoozes.
  void update(const DateTime &now);

  void snooze(const DateTime &now);
  void dismiss();

  AlarmState state() const { return state_; }
  int8_t ringingAlarmIndex() const { return ringingIndex_; }
  DateTime snoozeUntil() const { return snoozeUntil_; }

 private:
  void save();
  void load();

  Alarm alarms_[AlarmConfig::MaxAlarms];
  uint8_t snoozeMinutes_ = AlarmConfig::DefaultSnoozeMinutes;

  AlarmState state_ = AlarmState::Idle;
  int8_t ringingIndex_ = -1;
  DateTime snoozeUntil_;
  // Guards against re-triggering the same alarm repeatedly within the
  // minute it fires, since update() may be called many times per minute.
  // Holds unixtime()/60 of the last trigger, or -1 before the first one.
  int32_t lastTriggerMinute_ = -1;
};

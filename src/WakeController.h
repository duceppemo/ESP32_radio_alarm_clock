#pragma once

#include <RTClib.h>

#include "AlarmClock.h"
#include "AlarmSound.h"
#include "Config.h"
#include "RadioTuner.h"

// Coordinates AlarmClock's ringing state with RadioTuner (sunrise volume
// ramp, dead-air fallback) and AlarmSound (preset-tone wake sources).
// Doesn't own any of them -- pure orchestration -- so each stays testable
// and reusable on its own.
class WakeController {
 public:
  WakeController(AlarmClock &alarms, RadioTuner &radio, AlarmSound &sound);

  // Call once a second with the current time: drives the ramp and the
  // dead-air check. Also detects a ring/re-ring starting or ending, same as
  // tickFast() -- see that method for why both need to.
  void tickSlow(const DateTime &now);
  // Call every loop() iteration: keeps AlarmSound fed while it's playing,
  // and detects a ring/re-ring starting or ending. That detection also
  // happens (redundantly but harmlessly -- it's idempotent against
  // lastState_) in tickSlow(), which is throttled to once a second in
  // main.cpp; without it here too, dismissing/snoozing a beep/chime alarm
  // via a fast-path action (the menu's Home shortcut, the dashboard's
  // /api/alarm/dismiss) would leave the buzzer sounding for up to a second
  // after AlarmClock's state had already gone back to Idle.
  void tickFast();

 private:
  void detectRingTransition();
  void beginWake();
  void endWake();

  AlarmClock &alarms_;
  RadioTuner &radio_;
  AlarmSound &sound_;

  AlarmState lastState_ = AlarmState::Idle;
  bool wakeActive_ = false;
  WakeSource lastWakeSource_ = WakeSource::Radio;

  uint32_t wakeStartMs_ = 0;
  uint8_t rampTargetVolume_ = RadioConfig::DefaultVolume;
  bool deadAirChecked_ = false;
};

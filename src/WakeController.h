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
  // tickFast() -- see that method for why both need to. A Vol+/Vol-/
  // dashboard volume change noticed mid-ramp is treated as the user taking
  // manual control for the rest of this ring -- otherwise the ramp's own
  // per-second write would silently overwrite it a second later, making
  // the volume controls feel dead while an alarm is ringing.
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
  // newState is where AlarmClock landed after leaving Ringing -- Snoozed or
  // Idle -- so a radio wake can be handled differently for each: snoozing
  // should go quiet (the user is going back to sleep), while a real dismiss
  // restores the radio to its normal volume so it keeps playing as ambient
  // listening now that the user is up. Both currently reach here the same
  // way (detectRingTransition() only tracks Ringing vs not), so this needs
  // the actual landing state to tell them apart.
  void endWake(AlarmState newState);

  AlarmClock &alarms_;
  RadioTuner &radio_;
  AlarmSound &sound_;

  AlarmState lastState_ = AlarmState::Idle;
  bool wakeActive_ = false;
  WakeSource lastWakeSource_ = WakeSource::Radio;

  uint32_t wakeStartMs_ = 0;
  uint8_t rampTargetVolume_ = RadioConfig::DefaultVolume;
  bool deadAirChecked_ = false;
  // True once the dead-air fallback has muted the radio and started the
  // beep tone for the current ring -- endWake() checks this so dismissing
  // afterward doesn't unmute back onto whatever silence/static the station
  // actually was, the same "stay quiet" spirit as a snooze.
  bool deadAirFallbackTriggered_ = false;
  // The value this controller itself last wrote via setVolumeTransient() --
  // if radio.volume() reads anything else at the next tickSlow(), someone
  // else (Vol+/Vol-, the dashboard slider) changed it in between, and
  // that's the user taking over: leave volume alone for the rest of this
  // ring rather than have the ramp's own per-second write stomp it back a
  // second later. Compared against the live value, not persistedVolume(),
  // so a Vol+ that jumps to the already-saved volume (see
  // RadioTuner::volumeUp()) still registers even though nothing persisted
  // changed.
  uint8_t lastRampVolumeWritten_ = RadioConfig::DefaultVolume;
  bool manualVolumeOverride_ = false;
};

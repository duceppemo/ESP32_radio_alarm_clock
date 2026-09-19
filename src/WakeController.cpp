#include "WakeController.h"

WakeController::WakeController(AlarmClock &alarms, RadioTuner &radio, AlarmSound &sound)
    : alarms_(alarms), radio_(radio), sound_(sound) {}

void WakeController::tickSlow(const DateTime &now) {
  (void)now;  // ramp/dead-air timing below is all millis()-based
  detectRingTransition();

  if (!wakeActive_ || lastWakeSource_ != WakeSource::Radio) return;

  if (!manualVolumeOverride_ && radio_.volume() != lastRampVolumeWritten_) {
    manualVolumeOverride_ = true;  // see lastRampVolumeWritten_'s comment
  }

  if (!manualVolumeOverride_) {
    uint32_t elapsedMs = millis() - wakeStartMs_;
    uint32_t rampMs = (uint32_t)AlarmConfig::WakeRampSeconds * 1000UL;
    uint8_t v = rampTargetVolume_;
    if (elapsedMs < rampMs) {
      uint32_t deltaVol =
          (uint32_t)(rampTargetVolume_ - AlarmConfig::WakeRampStartVolume) * elapsedMs / rampMs;
      v = AlarmConfig::WakeRampStartVolume + (uint8_t)deltaVol;
    }
    radio_.setVolumeTransient(v);
    lastRampVolumeWritten_ = v;
  }

  if (!deadAirChecked_ && millis() - wakeStartMs_ >= (uint32_t)AlarmConfig::DeadAirCheckDelaySeconds * 1000UL) {
    deadAirChecked_ = true;
    if (radio_.rssi() < AlarmConfig::DeadAirRssiThreshold) {
      radio_.setMuted(true);
      deadAirFallbackTriggered_ = true;
      sound_.start(AlarmSound::Tone::ClassicBeep);
    }
  }
}

void WakeController::tickFast() {
  detectRingTransition();
  sound_.update();
}

void WakeController::detectRingTransition() {
  AlarmState state = alarms_.state();
  bool nowRinging = state == AlarmState::Ringing;
  bool wasRinging = lastState_ == AlarmState::Ringing;

  if (nowRinging && !wasRinging) {
    beginWake();
  } else if (!nowRinging && wasRinging) {
    endWake(state);
  }
  lastState_ = state;
}

void WakeController::beginWake() {
  wakeActive_ = true;
  wakeStartMs_ = millis();
  deadAirChecked_ = false;
  deadAirFallbackTriggered_ = false;
  manualVolumeOverride_ = false;

  int8_t idx = alarms_.ringingAlarmIndex();
  lastWakeSource_ = (idx >= 0) ? alarms_.alarm(idx).wakeSource : WakeSource::Radio;

  if (lastWakeSource_ == WakeSource::Radio && !radio_.available()) {
    // No tuner module on the bus -- there's nothing to ramp and the
    // dead-air check would only find RSSI 0 after DeadAirCheckDelaySeconds
    // of silence. Skip straight to what that check would do anyway.
    deadAirChecked_ = true;
    deadAirFallbackTriggered_ = true;
    radio_.setMuted(true);
    sound_.start(AlarmSound::Tone::ClassicBeep);
    return;
  }

  if (lastWakeSource_ == WakeSource::Radio) {
    // An alarm starting overrides any pending "turn off soon" wish -- left
    // active, the sleep timer could mute the radio mid-ring (RadioTuner::
    // update() doesn't know an alarm is in progress).
    radio_.cancelSleepTimer();
    // Ramp toward the user's real saved volume (persistedVolume()), not
    // volume() -- which, right after a snooze, is still whatever quiet
    // transient step the ramp had reached when it got interrupted. Ramping
    // toward that instead would ratchet the target down a little further
    // on every snooze. 0 is "effectively off", not a meaningful alarm
    // target, so it falls back to the documented sensible default instead
    // of leaving the alarm barely audible.
    uint8_t target = radio_.persistedVolume();
    if (target == 0) target = RadioConfig::DefaultVolume;
    rampTargetVolume_ = max<uint8_t>(target, AlarmConfig::WakeRampStartVolume + 1);
    radio_.setMuted(false);
    radio_.setVolumeTransient(AlarmConfig::WakeRampStartVolume);
    lastRampVolumeWritten_ = AlarmConfig::WakeRampStartVolume;
  } else {
    radio_.setMuted(true);
    sound_.start(lastWakeSource_ == WakeSource::Chime ? AlarmSound::Tone::Chime
                                                        : AlarmSound::Tone::ClassicBeep);
  }
}

void WakeController::endWake(AlarmState newState) {
  wakeActive_ = false;
  sound_.stop();
  if (lastWakeSource_ == WakeSource::Radio) {
    if (newState == AlarmState::Snoozed || deadAirFallbackTriggered_) {
      // Snoozing: go quiet rather than restoring volume -- the user is
      // going back to sleep, not settling in to listen. beginWake()
      // unmutes and restarts the ramp on its own once the snooze elapses
      // and it re-rings.
      // Dead-air fallback: the station turned out to be silent/static, so
      // unmuting back onto that on dismiss would just be an unpleasant
      // hiss -- stay muted until the user explicitly turns the radio back
      // on themselves.
      radio_.setMuted(true);
    } else {
      radio_.setVolumeTransient(rampTargetVolume_);
      radio_.setMuted(false);
    }
  }
}

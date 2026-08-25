#include "WakeController.h"

WakeController::WakeController(AlarmClock &alarms, RadioTuner &radio, AlarmSound &sound)
    : alarms_(alarms), radio_(radio), sound_(sound) {}

void WakeController::tickSlow(const DateTime &now) {
  AlarmState state = alarms_.state();
  bool nowRinging = state == AlarmState::Ringing;
  bool wasRinging = lastState_ == AlarmState::Ringing;

  if (nowRinging && !wasRinging) {
    beginWake();
  } else if (!nowRinging && wasRinging) {
    endWake();
  }
  lastState_ = state;

  if (!wakeActive_ || lastWakeSource_ != WakeSource::Radio) return;

  uint32_t elapsedMs = millis() - wakeStartMs_;
  uint32_t rampMs = (uint32_t)AlarmConfig::WakeRampSeconds * 1000UL;
  if (elapsedMs < rampMs) {
    uint32_t deltaVol =
        (uint32_t)(rampTargetVolume_ - AlarmConfig::WakeRampStartVolume) * elapsedMs / rampMs;
    radio_.setVolumeTransient(AlarmConfig::WakeRampStartVolume + (uint8_t)deltaVol);
  } else {
    radio_.setVolumeTransient(rampTargetVolume_);
  }

  if (!deadAirChecked_ && elapsedMs >= (uint32_t)AlarmConfig::DeadAirCheckDelaySeconds * 1000UL) {
    deadAirChecked_ = true;
    if (radio_.rssi() < AlarmConfig::DeadAirRssiThreshold) {
      radio_.setMuted(true);
      sound_.start(AlarmSound::Tone::ClassicBeep);
    }
  }
}

void WakeController::tickFast() { sound_.update(); }

void WakeController::beginWake() {
  wakeActive_ = true;
  wakeStartMs_ = millis();
  deadAirChecked_ = false;

  int8_t idx = alarms_.ringingAlarmIndex();
  lastWakeSource_ = (idx >= 0) ? alarms_.alarm(idx).wakeSource : WakeSource::Radio;

  if (lastWakeSource_ == WakeSource::Radio) {
    rampTargetVolume_ =
        max<uint8_t>(radio_.volume(), AlarmConfig::WakeRampStartVolume + 1);
    radio_.setMuted(false);
    radio_.setVolumeTransient(AlarmConfig::WakeRampStartVolume);
  } else {
    radio_.setMuted(true);
    sound_.start(lastWakeSource_ == WakeSource::Chime ? AlarmSound::Tone::Chime
                                                        : AlarmSound::Tone::ClassicBeep);
  }
}

void WakeController::endWake() {
  wakeActive_ = false;
  sound_.stop();
  if (lastWakeSource_ == WakeSource::Radio) {
    radio_.setVolumeTransient(rampTargetVolume_);
    radio_.setMuted(false);
  }
}

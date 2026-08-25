#include "AlarmClock.h"

#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "alarms";
constexpr const char *kSnoozeKey = "snoozeMin";
}  // namespace

void AlarmClock::begin() { load(); }

void AlarmClock::setAlarm(uint8_t index, const Alarm &alarm) {
  if (index >= count()) return;
  alarms_[index] = alarm;
  save();
}

void AlarmClock::setSnoozeMinutes(uint8_t minutes) {
  snoozeMinutes_ = minutes;
  save();
}

void AlarmClock::update(const DateTime &now) {
  if (state_ == AlarmState::Snoozed && now.unixtime() >= snoozeUntil_.unixtime()) {
    state_ = AlarmState::Ringing;
    return;
  }

  if (state_ != AlarmState::Idle) return;

  int32_t currentMinute = now.unixtime() / 60;
  if (currentMinute == lastTriggerMinute_) return;

  for (uint8_t i = 0; i < count(); i++) {
    const Alarm &a = alarms_[i];
    if (a.enabled && a.hour == now.hour() && a.minute == now.minute() &&
        a.activeOn(now.dayOfTheWeek())) {
      state_ = AlarmState::Ringing;
      ringingIndex_ = i;
      lastTriggerMinute_ = currentMinute;
      return;
    }
  }
}

void AlarmClock::snooze(const DateTime &now) {
  if (state_ != AlarmState::Ringing) return;
  state_ = AlarmState::Snoozed;
  snoozeUntil_ = now + TimeSpan(0, 0, snoozeMinutes_, 0);
}

void AlarmClock::dismiss() {
  state_ = AlarmState::Idle;
  ringingIndex_ = -1;
}

void AlarmClock::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kSnoozeKey, snoozeMinutes_);
  for (uint8_t i = 0; i < count(); i++) {
    char key[8];
    snprintf(key, sizeof(key), "a%u", i);
    prefs.putBytes(key, &alarms_[i], sizeof(Alarm));
  }
  prefs.end();
}

void AlarmClock::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  snoozeMinutes_ = prefs.getUChar(kSnoozeKey, AlarmConfig::DefaultSnoozeMinutes);
  for (uint8_t i = 0; i < count(); i++) {
    char key[8];
    snprintf(key, sizeof(key), "a%u", i);
    Alarm loaded;
    if (prefs.getBytesLength(key) == sizeof(Alarm)) {
      prefs.getBytes(key, &loaded, sizeof(Alarm));
      alarms_[i] = loaded;
    }
  }
  prefs.end();
}

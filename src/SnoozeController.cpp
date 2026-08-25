#include "SnoozeController.h"

SnoozeController::SnoozeController(AlarmClock &alarms, RadioTuner &radio)
    : alarms_(alarms), radio_(radio) {}

void SnoozeController::onSnoozePressed(const DateTime &now) {
  if (alarms_.state() != AlarmState::Idle) {
    alarms_.snooze(now);
    return;
  }

  if (radio_.muted()) return;

  if (radio_.sleepTimerActive()) {
    radio_.cancelSleepTimer();
  } else {
    radio_.setSleepTimer(RadioConfig::DefaultSleepTimerMinutes);
  }
}

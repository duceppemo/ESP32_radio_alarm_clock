#pragma once

#include <RTClib.h>

#include "AlarmClock.h"
#include "Config.h"
#include "RadioTuner.h"

// The single physical snooze button does different things depending on
// context, so its button-press handling doesn't belong inlined in main.cpp:
//   - An alarm is ringing/snoozed: snoozes it (the button's primary job).
//   - Otherwise, if the radio is on (unmuted): toggles a sleep timer, so
//     the same button doubles as "turn the radio off in N minutes" while
//     falling asleep to it. A second press cancels it.
//   - Otherwise (idle, radio off/muted): does nothing -- there's nothing to
//     snooze or sleep-time.
// Pure orchestration, owns neither dependency, same shape as WakeController.
class SnoozeController {
 public:
  SnoozeController(AlarmClock &alarms, RadioTuner &radio);

  // Call on the button's press edge (justPressed()), not every loop tick.
  void onSnoozePressed(const DateTime &now);

 private:
  AlarmClock &alarms_;
  RadioTuner &radio_;
};

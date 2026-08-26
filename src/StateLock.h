#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// RAII guard around one process-wide mutex protecting every touch of
// AlarmClock/RadioTuner/TimezoneStore/BatteryMonitor (and WebDashboard's own
// apMode_/credentials/restartAtMs_ etc.) from two different FreeRTOS tasks:
// the Arduino loop() task, and AsyncTCP's own "async_tcp" task, which is
// where WebDashboard's route handlers actually run -- a separate task from
// loop(), per esp32async/AsyncTCP's own README (it documents a core-affinity
// setting for that task). Without this, a dashboard request handled on that
// task could read or write the same objects loop() is touching at the same
// instant, with no happens-before guarantee between the two on a dual-core
// chip.
//
// Take it once at the top of loop() (covers everything loop() touches for
// that whole iteration -- MenuSystem, WakeController, AlarmClock, RadioTuner
// included -- none of which need their own locking) and once at the top of
// each WebDashboard route handler (each is invoked directly by the
// async_tcp task, not nested inside another already-locked call).
// WebDashboard's own internal helpers (syncTimeFromNtp(), the buildXJson()
// helpers, update(), etc.) deliberately do NOT take their own lock -- they're
// only ever called from a route handler or from loop() (via update()/
// statusLine()), so they inherit whichever entry point's lock is already
// held. This is a plain (non-recursive) mutex, so that discipline matters:
// never declare a second StateLock from a function that might run while one
// is already held on the same task, or it'll deadlock against itself.
//
// One coarse lock rather than one per module: request volume on this device
// is low enough that serializing behind it costs nothing real, and it
// avoids each module needing its own lock (fiddly, and NVS writes would
// then happen while holding one) or a queue (which would only solve the
// write side -- GET handlers reading live state face the identical race).
class StateLock {
 public:
  StateLock() { xSemaphoreTake(handle(), portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(handle()); }

  StateLock(const StateLock &) = delete;
  StateLock &operator=(const StateLock &) = delete;

 private:
  static SemaphoreHandle_t handle() {
    static SemaphoreHandle_t h = xSemaphoreCreateMutex();
    return h;
  }
};

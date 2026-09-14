#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <SI4735.h>

#include "Config.h"
#include "RegionStore.h"

// Thin wrapper around the SI4735 driver for the SI4730 FM tuner: adds preset
// storage/persistence and clamps everything to the FM band so callers don't
// have to poke the underlying library's raw 10 kHz units directly.
class RadioTuner {
 public:
  // region is read for FM de-emphasis/band/step at begin() and whenever
  // applyRegion() is called again after the region changes (see
  // RegionStore) -- RadioTuner holds a reference rather than owning it,
  // same as MenuSystem holding a TimezoneStore&.
  explicit RadioTuner(RegionStore &region) : region_(region) {}

  // Returns false if no SI4730/35 responded on the I2C bus at either its
  // known address -- e.g. the module isn't wired up yet. Every method
  // below checks available() internally and no-ops (or returns a zeroed
  // default) rather than touching the underlying driver when it's false --
  // the PU2CLR SI4735 library's waitToSend() polls the chip's Clear-To-Send
  // bit in an unbounded loop with no timeout, so calling into it with no
  // chip actually present hangs the calling task forever (this is exactly
  // what used to happen when the web dashboard queried radio state
  // unconditionally on every poll: an infinite busy-loop with no chip to
  // ever set that bit, eventually tripping whichever task's watchdog was
  // waiting on it). Callers still don't need to check available() first --
  // it's just for deciding whether to show/skip radio UI.
  bool begin(uint8_t resetPin = Pins::RadioReset);
  bool available() const { return available_; }

  void tune(uint16_t frequency10kHz);
  void seekUp();
  void seekDown();

  void setVolume(uint8_t volume);  // 0-63, persisted
  // Sets the volume without writing to flash -- for the sunrise ramp, which
  // would otherwise hit NVS every second.
  void setVolumeTransient(uint8_t volume);
  uint8_t volume() const { return volume_; }
  void volumeUp();
  void volumeDown();

  void setMuted(bool muted);
  bool muted() const { return muted_; }

  uint16_t frequency10kHz();
  float frequencyMHz() { return frequency10kHz() / 100.0f; }
  uint8_t rssi();

  uint8_t presetCount() const { return RadioConfig::MaxPresets; }
  uint16_t preset(uint8_t index) const { return presets_[index]; }
  void storePreset(uint8_t index, uint16_t frequency10kHz);
  void recallPreset(uint8_t index);

  // Pushes the current region's FM de-emphasis and band/step to the chip,
  // re-clamping the current frequency into the new band if needed. Called
  // once from begin(); call it again after changing region_'s selection
  // (RegionStore::setIndex() itself is just a persisted index, same as
  // TimezoneStore -- it doesn't know about RadioTuner or touch hardware).
  void applyRegion();

  // Sleep timer: mutes automatically once it elapses. update() must be
  // called periodically (main.cpp does this on the 1 Hz tick) to expire it.
  void setSleepTimer(uint16_t minutes);
  void cancelSleepTimer();
  bool sleepTimerActive() const { return sleepTimerEndMs_ != 0; }
  uint16_t sleepTimerRemainingMinutes() const;
  void update();

  // RDS Clock Time (CT) fallback sync -- see RegionStore.cpp/plan notes for
  // the full design. Call once a second from main.cpp's slow tick;
  // needsFallback is true when main.cpp/WebDashboard have decided there's
  // no reliable NTP time yet (RadioTuner intentionally has no WebDashboard
  // reference, so it can't decide this itself). Whenever the radio is on
  // and unmuted (the user is already listening), this harvests any CT
  // group for free regardless of needsFallback. Only when needsFallback is
  // true AND the radio is currently muted/idle does it periodically
  // re-assert the last-used frequency (to (re)start RDS acquisition) and
  // listen for a CT group for a bounded window -- never while the radio is
  // in active use, and any real user action cancels an in-progress attempt
  // immediately (see tune()/seekUp()/seekDown()/setMuted()).
  void updateRdsSync(bool needsFallback);
  // True exactly once after a plausible CT frame was decoded -- read
  // rdsTime() immediately after calling this.
  bool consumeRdsTimeSync();
  DateTime rdsTime() const { return rdsTime_; }

 private:
  void applyVolume(uint8_t volume);
  void save();
  void load();
  // Polls the RDS FIFO once; sets rdsTimeReady_ (and cancels an
  // in-progress fallback attempt, if any) on a plausible CT frame.
  void pollRdsForTime();

  SI4735 si4735_;
  RegionStore &region_;
  bool available_ = false;
  uint8_t volume_ = RadioConfig::DefaultVolume;
  bool muted_ = false;
  uint16_t presets_[RadioConfig::MaxPresets] = {};
  uint32_t sleepTimerEndMs_ = 0;  // 0 = inactive

  bool rdsFallbackActive_ = false;
  uint32_t rdsFallbackStartMs_ = 0;
  uint32_t lastRdsFallbackAttemptMs_ = 0;  // 0 = never attempted yet this boot
  bool rdsTimeReady_ = false;
  DateTime rdsTime_;
};

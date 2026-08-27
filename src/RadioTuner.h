#pragma once

#include <Arduino.h>
#include <SI4735.h>

#include "Config.h"

// Thin wrapper around the SI4735 driver for the SI4730 FM tuner: adds preset
// storage/persistence and clamps everything to the FM band so callers don't
// have to poke the underlying library's raw 10 kHz units directly.
class RadioTuner {
 public:
  // Returns false if no SI4730/35 responded on the I2C bus at either its
  // known address -- e.g. the module isn't wired up yet. Callers should
  // check available() before trusting anything below to reflect a real
  // chip; the setters still won't crash if called anyway (they just talk
  // to a driver object backed by nothing), but frequency10kHz()/rssi()/etc.
  // won't mean anything either.
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

  // Sleep timer: mutes automatically once it elapses. update() must be
  // called periodically (main.cpp does this on the 1 Hz tick) to expire it.
  void setSleepTimer(uint16_t minutes);
  void cancelSleepTimer();
  bool sleepTimerActive() const { return sleepTimerEndMs_ != 0; }
  uint16_t sleepTimerRemainingMinutes() const;
  void update();

 private:
  void applyVolume(uint8_t volume);
  void save();
  void load();

  SI4735 si4735_;
  bool available_ = false;
  uint8_t volume_ = RadioConfig::DefaultVolume;
  bool muted_ = false;
  uint16_t presets_[RadioConfig::MaxPresets] = {};
  uint32_t sleepTimerEndMs_ = 0;  // 0 = inactive
};

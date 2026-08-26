#include "RadioTuner.h"

#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "radio";
constexpr const char *kVolumeKey = "volume";
constexpr const char *kFreqKey = "freq";
}  // namespace

void RadioTuner::begin(uint8_t resetPin) {
  load();

  si4735_.setup(resetPin, FM_CURRENT_MODE);
  si4735_.setFM(RadioConfig::FmBandStart, RadioConfig::FmBandEnd,
                RadioConfig::FmDefaultFreq, RadioConfig::FmStep);
  si4735_.setVolume(volume_);

  Preferences prefs;
  prefs.begin(kNamespace, true);
  uint16_t lastFreq = prefs.getUShort(kFreqKey, RadioConfig::FmDefaultFreq);
  prefs.end();
  tune(lastFreq);
}

void RadioTuner::tune(uint16_t frequency10kHz) {
  frequency10kHz = constrain(frequency10kHz, RadioConfig::FmBandStart, RadioConfig::FmBandEnd);
  si4735_.setFrequency(frequency10kHz);

  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUShort(kFreqKey, frequency10kHz);
  prefs.end();
}

void RadioTuner::seekUp() { si4735_.seekStationUp(); }
void RadioTuner::seekDown() { si4735_.seekStationDown(); }

void RadioTuner::setVolume(uint8_t volume) {
  applyVolume(volume);
  save();
}

void RadioTuner::setVolumeTransient(uint8_t volume) { applyVolume(volume); }

void RadioTuner::applyVolume(uint8_t volume) {
  volume_ = min<uint8_t>(volume, 63);
  si4735_.setVolume(volume_);
}

void RadioTuner::volumeUp() { setVolume(min<uint8_t>(volume_ + 1, 63)); }
void RadioTuner::volumeDown() { setVolume(volume_ > 0 ? volume_ - 1 : 0); }

void RadioTuner::setMuted(bool muted) {
  muted_ = muted;
  si4735_.setAudioMute(muted_);
}

uint16_t RadioTuner::frequency10kHz() { return si4735_.getFrequency(); }
uint8_t RadioTuner::rssi() { return si4735_.getCurrentRSSI(); }

void RadioTuner::storePreset(uint8_t index, uint16_t frequency10kHz) {
  if (index >= presetCount()) return;
  presets_[index] = frequency10kHz;

  Preferences prefs;
  prefs.begin(kNamespace, false);
  char key[8];
  snprintf(key, sizeof(key), "p%u", index);
  prefs.putUShort(key, frequency10kHz);
  prefs.end();
}

void RadioTuner::recallPreset(uint8_t index) {
  if (index >= presetCount() || presets_[index] == 0) return;
  tune(presets_[index]);
}

void RadioTuner::setSleepTimer(uint16_t minutes) {
  if (minutes == 0) {
    // Callers are expected to route 0 to cancelSleepTimer() themselves, but
    // an int-to-uint16_t truncation upstream (e.g. the dashboard casting a
    // JSON value of 65536) can land here with 0 anyway. Without this guard,
    // millis() + 0 is still a nonzero "deadline" already in the past, so the
    // very next update() would mute the radio immediately instead of
    // leaving the sleep timer inactive.
    cancelSleepTimer();
    return;
  }
  minutes = min(minutes, RadioConfig::MaxSleepTimerMinutes);
  sleepTimerEndMs_ = millis() + (uint32_t)minutes * 60000UL;
}

void RadioTuner::cancelSleepTimer() { sleepTimerEndMs_ = 0; }

uint16_t RadioTuner::sleepTimerRemainingMinutes() const {
  if (sleepTimerEndMs_ == 0) return 0;
  uint32_t nowMs = millis();
  if (nowMs >= sleepTimerEndMs_) return 0;
  // Ceiling division: exactly N minutes left reads as N, not N+1.
  return (sleepTimerEndMs_ - nowMs + 60000UL - 1) / 60000UL;
}

void RadioTuner::update() {
  if (sleepTimerEndMs_ != 0 && millis() >= sleepTimerEndMs_) {
    setMuted(true);
    sleepTimerEndMs_ = 0;
  }
}

void RadioTuner::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kVolumeKey, volume_);
  prefs.end();
}

void RadioTuner::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  volume_ = prefs.getUChar(kVolumeKey, RadioConfig::DefaultVolume);
  for (uint8_t i = 0; i < presetCount(); i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%u", i);
    presets_[i] = prefs.getUShort(key, 0);
  }
  prefs.end();
}

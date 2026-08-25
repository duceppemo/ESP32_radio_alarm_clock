#pragma once

#include <ESP_I2S.h>

#include "Config.h"

// Generates simple alarm tones on the fly and streams them to the STEMMA
// I2S amp -- used both as a selectable wake sound (instead of the radio)
// and as the dead-air fallback when the radio has no signal. Synthesizes
// samples in small chunks (ToneConfig::ChunkSamples) rather than
// precomputing a buffer, so update() stays cheap and non-blocking-ish.
class AlarmSound {
 public:
  enum class Tone : uint8_t { ClassicBeep = 1, Chime = 2 };

  bool begin();

  void start(Tone tone);
  void stop();
  bool active() const { return active_; }

  // Call every loop() iteration while active(); writes the next chunk to I2S.
  void update();

 private:
  void fillChunk(int16_t *buffer, uint16_t samples);

  I2SClass i2s_;
  bool ready_ = false;
  bool active_ = false;
  Tone tone_ = Tone::ClassicBeep;
  uint32_t patternElapsedMs_ = 0;
  float phase_ = 0.0f;
};

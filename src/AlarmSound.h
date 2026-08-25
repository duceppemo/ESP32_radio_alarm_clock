#pragma once

#include <Arduino.h>

#include "Config.h"

// Drives a piezo buzzer wired directly to a GPIO pin (Pins::Buzzer) via
// Arduino's tone()/noTone(). Entirely separate from FM/AM playback, which
// runs SI4730 (analog audio out) -> amp -> speaker and never touches the
// ESP32 -- see the note in Config.h. Used both as a selectable wake sound
// and as the dead-air fallback when the radio has no signal.
class AlarmSound {
 public:
  enum class Tone : uint8_t { ClassicBeep = 1, Chime = 2 };

  // One step of a repeating pattern: freqHz == 0 means silence.
  struct Step {
    uint16_t freqHz;
    uint16_t durationMs;
  };

  bool begin();

  void start(Tone tone);
  void stop();
  bool active() const { return active_; }
  Tone currentTone() const { return tone_; }

  // Call every loop() iteration while active(); advances the tone pattern.
  void update();

 private:
  const Step *pattern() const;
  uint8_t patternLength() const;

  Tone tone_ = Tone::ClassicBeep;
  bool active_ = false;
  uint32_t patternStartMs_ = 0;
  uint32_t cycleMs_ = 0;
  int8_t lastStepIndex_ = -1;
};

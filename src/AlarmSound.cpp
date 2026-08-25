#include "AlarmSound.h"

namespace {
// Alternating two-tone warble, close to what commercial alarm clocks use.
// Kept around 1.8-2.2kHz rather than a piezo's ~3-4kHz resonant peak, so
// it's insistent without being painful.
constexpr AlarmSound::Step kBeepPattern[] = {
    {1800, 150},
    {2200, 150},
};

// Gentle ascending A-major triad ("ding-ding-dong"), then a pause -- a
// softer wake, and a friendlier dead-air fallback than a flat buzz.
constexpr AlarmSound::Step kChimePattern[] = {
    {880, 150},   // A5
    {0, 50},
    {1109, 150},  // C#6
    {0, 50},
    {1319, 300},  // E6
    {0, 600},
};

template <typename T, size_t N>
constexpr size_t arraySize(const T (&)[N]) {
  return N;
}
}  // namespace

bool AlarmSound::begin() {
  pinMode(Pins::Buzzer, OUTPUT);
  return true;
}

void AlarmSound::start(Tone tone) {
  tone_ = tone;
  patternStartMs_ = millis();
  lastStepIndex_ = -1;
  active_ = true;

  cycleMs_ = 0;
  const Step *steps = pattern();
  for (uint8_t i = 0; i < patternLength(); i++) cycleMs_ += steps[i].durationMs;
}

void AlarmSound::stop() {
  active_ = false;
  noTone(Pins::Buzzer);
}

const AlarmSound::Step *AlarmSound::pattern() const {
  return tone_ == Tone::Chime ? kChimePattern : kBeepPattern;
}

uint8_t AlarmSound::patternLength() const {
  return tone_ == Tone::Chime ? arraySize(kChimePattern) : arraySize(kBeepPattern);
}

void AlarmSound::update() {
  if (!active_ || cycleMs_ == 0) return;

  const Step *steps = pattern();
  uint8_t count = patternLength();
  uint32_t t = (millis() - patternStartMs_) % cycleMs_;

  uint8_t index = 0;
  uint32_t acc = 0;
  for (; index < count - 1; index++) {
    acc += steps[index].durationMs;
    if (t < acc) break;
  }

  if (index == lastStepIndex_) return;
  lastStepIndex_ = index;

  uint16_t freq = steps[index].freqHz;
  if (freq == 0) {
    noTone(Pins::Buzzer);
  } else {
    tone(Pins::Buzzer, freq);
  }
}

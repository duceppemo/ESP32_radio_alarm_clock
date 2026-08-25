#include "AlarmSound.h"

#include <math.h>

namespace {
constexpr float kPi = 3.14159265f;
constexpr int16_t kAmplitude = 8000;  // headroom below int16 full-scale to avoid clipping
}  // namespace

bool AlarmSound::begin() {
  i2s_.setPins(Pins::AmpI2sBclk, Pins::AmpI2sLrc, Pins::AmpI2sDin);
  ready_ = i2s_.begin(I2S_MODE_STD, ToneConfig::SampleRateHz, I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_MONO);
  return ready_;
}

void AlarmSound::start(Tone tone) {
  if (!ready_) return;
  tone_ = tone;
  patternElapsedMs_ = 0;
  phase_ = 0.0f;
  active_ = true;
}

void AlarmSound::stop() { active_ = false; }

void AlarmSound::update() {
  if (!active_ || !ready_) return;
  int16_t buffer[ToneConfig::ChunkSamples];
  fillChunk(buffer, ToneConfig::ChunkSamples);
  i2s_.write(reinterpret_cast<const uint8_t *>(buffer), sizeof(buffer));
  patternElapsedMs_ += (ToneConfig::ChunkSamples * 1000UL) / ToneConfig::SampleRateHz;
}

void AlarmSound::fillChunk(int16_t *buffer, uint16_t samples) {
  uint32_t cycleMs = (tone_ == Tone::Chime) ? 800 : 500;

  for (uint16_t i = 0; i < samples; i++) {
    uint32_t tMs = (patternElapsedMs_ + (i * 1000UL) / ToneConfig::SampleRateHz) % cycleMs;
    float freq;
    bool on = true;

    if (tone_ == Tone::Chime) {
      freq = (tMs < 400) ? 660.0f : 880.0f;  // two-note chime, no gap
    } else {
      on = tMs < 300;  // 300ms on / 200ms off beep
      freq = 800.0f;
    }

    if (!on) {
      buffer[i] = 0;
      continue;
    }
    phase_ += 2.0f * kPi * freq / ToneConfig::SampleRateHz;
    if (phase_ > 2.0f * kPi) phase_ -= 2.0f * kPi;
    buffer[i] = static_cast<int16_t>(kAmplitude * sinf(phase_));
  }
}

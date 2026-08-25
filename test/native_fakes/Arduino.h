#pragma once

// Minimal native stand-in for Arduino.h. Only implements what the tested
// modules actually call: the basic integer types, the board pin macros
// Config.h's Pins:: namespace references (exact values don't matter --
// nothing under test reads real hardware through them), a controllable
// fake millis() and per-pin digitalRead() so timing/button logic (sleep
// timer, wake ramp, DebouncedButton) can be tested without real delays or
// hardware, no-op GPIO/tone functions, and min/max templates matching how
// the real Arduino core lets them be called with an explicit template
// argument (e.g. min<uint8_t>(a, b)).

#include <cstdint>
#include <string>

// Minimal stand-in for Arduino's String -- just enough to construct from a
// literal and hand off to Print::println().
class String {
 public:
  String() = default;
  String(const char *s) : data_(s ? s : "") {}
  const char *c_str() const { return data_.c_str(); }

 private:
  std::string data_;
};

constexpr uint8_t A0 = 0;
constexpr uint8_t A1 = 1;
constexpr uint8_t A2 = 2;
constexpr uint8_t A3 = 3;
constexpr uint8_t A4 = 4;
constexpr uint8_t A5 = 5;
constexpr uint8_t RX = 6;
constexpr uint8_t TX = 7;

constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT = 0;
constexpr uint8_t INPUT_PULLUP = 2;
constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;

inline void pinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
}
inline void digitalWrite(uint8_t pin, uint8_t value) {
  (void)pin;
  (void)value;
}
inline void tone(uint8_t pin, unsigned int frequency) {
  (void)pin;
  (void)frequency;
}
inline void noTone(uint8_t pin) { (void)pin; }

// Per-pin simulated digital input state, settable from tests (buttons are
// active-low with internal pull-ups, so HIGH = not pressed is the default).
inline uint8_t &native_fake_digital_state(uint8_t pin) {
  static uint8_t states[16];
  static bool initialized = false;
  if (!initialized) {
    for (auto &s : states) s = HIGH;
    initialized = true;
  }
  return states[pin];
}
inline uint8_t digitalRead(uint8_t pin) { return native_fake_digital_state(pin); }

// Test-controlled fake clock, read by millis().
inline uint32_t &native_fake_millis_value() {
  static uint32_t value = 0;
  return value;
}
inline uint32_t millis() { return native_fake_millis_value(); }

template <typename T>
constexpr T min(T a, T b) {
  return a < b ? a : b;
}
template <typename T>
constexpr T max(T a, T b) {
  return a > b ? a : b;
}

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#pragma once

#include <Arduino.h>

// Debounced digital input with press/release edges. Defaults to active-low
// (buttons wired to GND with an internal pull-up) -- true for the snooze/
// volume panel buttons and the Reverse TFT Feather's own D0 button. That
// board's D1/D2 buttons are wired the opposite way (external pull-down,
// HIGH when pressed), so those two need activeHigh=true.
class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin, bool activeHigh = false)
      : pin_(pin), activeHigh_(activeHigh) {}

  void update() {
    bool reading = activeHigh_ ? digitalRead(pin_) == HIGH : digitalRead(pin_) == LOW;
    if (reading != stableState_ && millis() - lastChangeMs_ > kDebounceMs) {
      stableState_ = reading;
      lastChangeMs_ = millis();
      justPressed_ = stableState_;
      justReleased_ = !stableState_;
      if (justPressed_) nextRepeatMs_ = millis() + kRepeatDelayMs;
      return;
    }
    justPressed_ = false;
    justReleased_ = false;
  }

  bool justPressed() const { return justPressed_; }
  bool justReleased() const { return justReleased_; }
  bool isDown() const { return stableState_; }

  // True on the initial press, and again periodically while held past an
  // initial delay -- for auto-repeating value-adjustment buttons (Set
  // Time, alarm fields, radio tuning, timezone cycling), so holding up/down
  // keeps changing the value instead of needing repeated taps. Has a side
  // effect (advances the internal repeat schedule when it fires), so call
  // it exactly once per update() cycle and reuse the result -- not once per
  // place that needs it.
  bool triggered() {
    if (justPressed_) return true;
    if (stableState_ && millis() >= nextRepeatMs_) {
      nextRepeatMs_ = millis() + kRepeatIntervalMs;
      return true;
    }
    return false;
  }

 private:
  static constexpr uint16_t kDebounceMs = 30;
  static constexpr uint16_t kRepeatDelayMs = 450;    // hold time before repeat kicks in
  static constexpr uint16_t kRepeatIntervalMs = 150; // cadence once it has
  uint8_t pin_;
  bool activeHigh_;
  bool stableState_ = false;
  bool justPressed_ = false;
  bool justReleased_ = false;
  uint32_t lastChangeMs_ = 0;
  uint32_t nextRepeatMs_ = 0;
};

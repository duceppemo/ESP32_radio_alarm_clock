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
      return;
    }
    justPressed_ = false;
    justReleased_ = false;
  }

  bool justPressed() const { return justPressed_; }
  bool justReleased() const { return justReleased_; }
  bool isDown() const { return stableState_; }

 private:
  static constexpr uint16_t kDebounceMs = 30;
  uint8_t pin_;
  bool activeHigh_;
  bool stableState_ = false;
  bool justPressed_ = false;
  bool justReleased_ = false;
  uint32_t lastChangeMs_ = 0;
};

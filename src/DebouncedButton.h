#pragma once

#include <Arduino.h>

// Debounced digital input with press/release edges. Active-low (buttons
// wired to GND with an internal pull-up) -- shared by MenuSystem (onboard
// menu buttons) and main.cpp (panel snooze/volume buttons).
class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin) : pin_(pin) {}

  void update() {
    bool reading = digitalRead(pin_) == LOW;
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
  bool stableState_ = false;
  bool justPressed_ = false;
  bool justReleased_ = false;
  uint32_t lastChangeMs_ = 0;
};

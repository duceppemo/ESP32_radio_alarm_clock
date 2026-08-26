#pragma once

#include <Arduino.h>

// Pure math mapping an ambient-light reading (VEML7700 lux) to the
// brightness level for each display. Hardware-independent -- main.cpp is
// the one that actually calls analogWrite()/Adafruit_7segment::setBrightness()
// with these values, so this stays testable without a real sensor/display.
class DisplayDimmer {
 public:
  // 0-255 PWM duty cycle for the TFT backlight.
  static uint8_t tftBacklightFor(float lux);
  // 0-15, the HT16K33 LED backpack's native brightness range.
  static uint8_t sevenSegmentBrightnessFor(float lux);
};

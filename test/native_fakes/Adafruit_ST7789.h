#pragma once

#include "Adafruit_GFX.h"

constexpr uint16_t ST77XX_BLACK = 0x0000;
constexpr uint16_t ST77XX_WHITE = 0xFFFF;
constexpr uint16_t ST77XX_RED = 0xF800;

// Minimal native stand-in for Adafruit_ST7789 -- see Adafruit_GFX.h for why.
class Adafruit_ST7789 : public Adafruit_GFX {
 public:
  Adafruit_ST7789(int8_t cs, int8_t dc, int8_t rst) : Adafruit_GFX(240, 135) {
    (void)cs;
    (void)dc;
    (void)rst;
  }

  bool init(uint16_t width, uint16_t height) {
    (void)width;
    (void)height;
    return true;
  }
  void setRotation(uint8_t rotation) { (void)rotation; }
};

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "Arduino.h"

// Minimal native stand-in for the Print -> Adafruit_GFX chain, just enough
// for MenuSystem's render() calls (fillScreen/setCursor/setTextColor/
// setTextSize/print/println/printf) to compile and run without a real
// display. Output is discarded -- these tests care about the button-driven
// state machine's effect on AlarmClock/RadioTuner, not pixels.
class Print {
 public:
  virtual ~Print() = default;

  size_t print(const char *s) { return s ? strlen(s) : 0; }
  size_t print(int value) {
    char buf[16];
    return snprintf(buf, sizeof(buf), "%d", value);
  }
  size_t println() { return 0; }
  size_t println(const char *s) { return print(s); }
  size_t println(const String &s) { return print(s.c_str()); }
  size_t printf(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }

 private:
  static size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
  }
};

class Adafruit_GFX : public Print {
 public:
  Adafruit_GFX(int16_t w, int16_t h) : width_(w), height_(h) {}

  void fillScreen(uint16_t color) { (void)color; }
  void setCursor(int16_t x, int16_t y) {
    (void)x;
    (void)y;
  }
  void setTextColor(uint16_t color) { (void)color; }
  void setTextSize(uint8_t size) { (void)size; }
  void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
    (void)x;
    (void)y;
    (void)bitmap;
    (void)w;
    (void)h;
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)color;
  }

 private:
  int16_t width_, height_;
};

// Minimal stand-in for the off-screen-buffer companion class MenuSystem
// composes each frame into before blitting it to the real display in one
// shot. getBuffer() just needs to return a non-null pointer of the right
// size -- nothing inspects its contents natively.
class GFXcanvas16 : public Adafruit_GFX {
 public:
  GFXcanvas16(uint16_t w, uint16_t h) : Adafruit_GFX(w, h), buffer_(new uint16_t[w * h]) {}
  ~GFXcanvas16() { delete[] buffer_; }

  uint16_t *getBuffer() const { return buffer_; }

 private:
  uint16_t *buffer_;
};

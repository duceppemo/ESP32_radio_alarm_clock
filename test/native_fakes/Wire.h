#pragma once

#include <cstdint>

// Compile-only stand-in -- RadioTuner::readRdsGroupSafely() is the one
// place this project talks to Wire directly rather than through a wrapped
// driver object, and it exists purely so RadioTuner.cpp (part of the
// native build_src_filter) still compiles natively. Nothing here needs to
// be test-controllable: all the logic worth testing (RDS group decoding)
// lives in decodeRdsGroup(), a pure function that never touches this.
class TwoWire {
 public:
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission() { return 0; }
  size_t write(uint8_t) { return 1; }
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};

inline TwoWire Wire;

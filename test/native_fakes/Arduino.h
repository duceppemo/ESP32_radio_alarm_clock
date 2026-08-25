#pragma once

// Minimal native stand-in for Arduino.h -- AlarmClock's logic doesn't call
// any real Arduino API (no millis(), no digitalWrite(), ...), it only needs
// the basic integer types and the board pin macros that Config.h's Pins::
// namespace references. Those pin values are never touched by AlarmClock's
// logic, so their exact numbers don't matter here -- they just need to
// exist so Config.h compiles.

#include <cstdint>

constexpr uint8_t A0 = 0;
constexpr uint8_t A1 = 1;
constexpr uint8_t A2 = 2;
constexpr uint8_t A3 = 3;
constexpr uint8_t A4 = 4;
constexpr uint8_t A5 = 5;
constexpr uint8_t RX = 6;
constexpr uint8_t TX = 7;

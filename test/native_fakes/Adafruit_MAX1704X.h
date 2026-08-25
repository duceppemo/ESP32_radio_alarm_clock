#pragma once

// Minimal native stand-in for the Adafruit MAX1704X driver -- BatteryMonitor
// holds one by value, so MenuSystem.h (which holds a BatteryMonitor*) needs
// a full type even when tests pass a null BatteryMonitor*. BatteryMonitor.cpp
// itself IS compiled for the native env too (platformio.ini's
// build_src_filter) -- MenuSystem.cpp calls through the pointer, so the
// linker needs those symbols even though no test triggers them at runtime.
class Adafruit_MAX17048 {
 public:
  bool begin() { return false; }
  float cellVoltage() { return 0.0f; }
  float cellPercent() { return 0.0f; }
};

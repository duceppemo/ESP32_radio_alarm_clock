#pragma once

// Minimal native stand-in for the Adafruit MAX1704X driver -- BatteryMonitor
// is a value member of it, so MenuSystem.h (which holds a BatteryMonitor*)
// needs a full type even though nothing under test calls into it.
// BatteryMonitor.cpp itself isn't compiled for the native env.
class Adafruit_MAX17048 {
 public:
  bool begin() { return false; }
  float cellVoltage() { return 0.0f; }
  float cellPercent() { return 0.0f; }
};

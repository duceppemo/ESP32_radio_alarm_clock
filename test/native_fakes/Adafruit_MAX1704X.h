#pragma once

// Minimal native stand-in for the Adafruit MAX1704X driver -- BatteryMonitor
// holds one by value, so MenuSystem.h (which holds a BatteryMonitor*) needs
// a full type even when tests pass a null BatteryMonitor*. BatteryMonitor.cpp
// itself IS compiled for the native env too (platformio.ini's
// build_src_filter) -- MenuSystem.cpp calls through the pointer, so the
// linker needs those symbols even when no test triggers them at runtime.
//
// Also directly testable via test_battery_monitor, which controls what "the
// chip" reports back the same way SI4735.h's setSimulatedRssi() does for
// RadioTuner.
class Adafruit_MAX17048 {
 public:
  bool begin() { return simulatedBeginOk(); }
  float cellVoltage() { return simulatedVoltage(); }
  float cellPercent() { return simulatedPercent(); }

  static void setSimulatedBeginOk(bool ok) { simulatedBeginOk() = ok; }
  static void setSimulatedVoltage(float v) { simulatedVoltage() = v; }
  static void setSimulatedPercent(float p) { simulatedPercent() = p; }
  static void resetSimulated() {
    simulatedBeginOk() = true;
    simulatedVoltage() = 4.0f;
    simulatedPercent() = 100.0f;
  }

 private:
  static bool &simulatedBeginOk() {
    static bool v = true;
    return v;
  }
  static float &simulatedVoltage() {
    static float v = 4.0f;
    return v;
  }
  static float &simulatedPercent() {
    static float v = 100.0f;
    return v;
  }
};

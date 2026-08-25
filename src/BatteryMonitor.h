#pragma once

#include <Adafruit_MAX1704X.h>

#include "Config.h"

// Wraps the onboard MAX17048 LiPoly fuel gauge (I2C address 0x36) that ships
// on the Adafruit ESP32-S3 Reverse TFT Feather -- gives an accurate percent
// and voltage reading, no ADC voltage divider needed.
class BatteryMonitor {
 public:
  bool begin();

  float voltage();     // volts
  float percent();     // 0-100
  bool isLow();         // percent() below BatteryConfig::LowPercentThreshold

  bool available() const { return available_; }

 private:
  Adafruit_MAX17048 gauge_;
  bool available_ = false;
};

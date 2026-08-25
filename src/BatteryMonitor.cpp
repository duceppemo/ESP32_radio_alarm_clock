#include "BatteryMonitor.h"

bool BatteryMonitor::begin() {
  available_ = gauge_.begin();
  return available_;
}

float BatteryMonitor::voltage() {
  if (!available_) return 0.0f;
  return gauge_.cellVoltage();
}

float BatteryMonitor::percent() {
  if (!available_) return 0.0f;
  return gauge_.cellPercent();
}

bool BatteryMonitor::isLow() {
  if (!available_) return false;
  return percent() < BatteryConfig::LowPercentThreshold;
}

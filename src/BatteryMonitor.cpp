#include "BatteryMonitor.h"

namespace {
// Measured on real hardware (both readings taken a couple seconds after
// boot, well past any power-on transient):
//   battery attached:  voltage 4.021V, percent 79.8%
//   no battery, USB only: voltage 4.137V, percent 103.0%
// percent() is a 0-100 state-of-charge estimate, so anything meaningfully
// over 100% is impossible for a real attached cell -- 103.0% vs. 79.8% is
// a wide, clean margin. (Voltage alone isn't reliable here: a genuinely
// full battery can legitimately approach the no-battery case's ~4.14V, so
// this deliberately doesn't gate on voltage.) The threshold leaves
// headroom above 100 for the small overshoot the MAX17048 is documented
// to sometimes show right after a real cell finishes charging.
constexpr float kMaxPlausiblePercent = 101.0f;
}  // namespace

// NOTE: a battery plugged/unplugged after boot is NOT detected -- only the
// state at begin() is. Three different live-resync approaches were tried
// (quickStart() on demand, quickStart() on a timer, periodically repeating
// begin() itself) and none were worth keeping: the first two never
// reliably forced the gauge's SOC estimate to reflect a live swap, and the
// third worked but made the reading visibly drop to 0 for about a second
// on every resync, even with a battery attached the whole time -- a worse
// user experience than just not detecting a live swap at all. Detecting
// this properly would need a hardware voltage divider to a spare ADC pin
// (this board doesn't have one built in, unlike some older Feather
// boards), not another software workaround against this chip.
bool BatteryMonitor::begin() {
  available_ = gauge_.begin();
  if (available_) {
    // The first cellVoltage()/cellPercent() read right after begin() can
    // still reflect the chip's power-on-reset defaults rather than a real
    // conversion -- reading that as "implausible" would incorrectly flag
    // a perfectly healthy attached battery as absent right at boot.
    delay(200);
  }
  return available_;
}

bool BatteryMonitor::available() {
  return available_ && gauge_.cellPercent() <= kMaxPlausiblePercent;
}

float BatteryMonitor::voltage() {
  if (!available()) return 0.0f;
  return gauge_.cellVoltage();
}

float BatteryMonitor::percent() {
  if (!available()) return 0.0f;
  return gauge_.cellPercent();
}

bool BatteryMonitor::isLow() {
  if (!available()) return false;
  return percent() < BatteryConfig::LowPercentThreshold;
}

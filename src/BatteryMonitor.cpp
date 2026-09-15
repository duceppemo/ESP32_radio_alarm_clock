#include "BatteryMonitor.h"

namespace {
// Measured on real hardware:
//   battery attached, partial charge: voltage 4.021V, percent 79.8%
//   battery attached, fully charged:  voltage 4.19-4.20V (stable),
//                                      percent 101.6% (stable)
//   no battery, USB only:             voltage 4.10-4.19V (noisy, jumping
//                                      ~50-80mV between readings a second
//                                      apart -- a floating sense node has
//                                      nothing smoothing it, unlike a real
//                                      cell), percent 104.5-104.8%
// percent() is a 0-100 state-of-charge estimate, so anything meaningfully
// over 100% is impossible for a real attached cell -- but a fully charged
// real cell can overshoot 100% by a couple of points right after finishing
// charging (per the first pair above), so the cutoff needs headroom for
// that without also accepting the no-battery case. 103.0 sits in the
// middle of that measured gap (101.6 vs. 104.5+), the widest margin
// available from a single percent() cutoff. (Voltage alone still isn't
// reliable -- the no-battery range overlaps the low end of what a real
// full battery can read -- so this deliberately doesn't gate on voltage.)
constexpr float kMaxPlausiblePercent = 103.0f;
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
  // The raw gauge estimate can overshoot 100 right after a full charge
  // (see kMaxPlausiblePercent's comment) -- available() needs that real
  // overshoot to tell a genuine battery apart from no battery at all, but
  // nothing displaying this to a person should ever show more than 100%.
  return min(gauge_.cellPercent(), 100.0f);
}

bool BatteryMonitor::isLow() {
  if (!available()) return false;
  return percent() < BatteryConfig::LowPercentThreshold;
}

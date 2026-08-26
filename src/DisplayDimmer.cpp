#include "DisplayDimmer.h"

#include <math.h>

#include "Config.h"

namespace {
uint8_t mapLux(float lux, uint8_t minOut, uint8_t maxOut) {
  if (lux <= DisplayConfig::DimLuxThreshold) return minOut;
  if (lux >= DisplayConfig::BrightLuxThreshold) return maxOut;
  float t = (lux - DisplayConfig::DimLuxThreshold) /
            (DisplayConfig::BrightLuxThreshold - DisplayConfig::DimLuxThreshold);
  return (uint8_t)(minOut + lroundf(t * (float)(maxOut - minOut)));
}
}  // namespace

uint8_t DisplayDimmer::tftBacklightFor(float lux) {
  return mapLux(lux, DisplayConfig::MinTftBacklight, DisplayConfig::MaxTftBacklight);
}

uint8_t DisplayDimmer::sevenSegmentBrightnessFor(float lux) {
  return mapLux(lux, DisplayConfig::MinSevenSegmentBrightness,
                DisplayConfig::MaxSevenSegmentBrightness);
}

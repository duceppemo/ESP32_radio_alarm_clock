#include "RegionStore.h"

#include <Preferences.h>

#include "Config.h"

namespace {
constexpr const char *kNamespace = "region";
constexpr const char *kIndexKey = "index";

// FM bands are identical (87.5-108.0MHz) for Americas and Europe/RoW --
// only de-emphasis differs, so both reference RadioConfig's default band.
// Japan's is genuinely narrower and shifted (76.0-95.0MHz), which is why
// RadioTuner::tune() clamps against the *current* region's band rather
// than a fixed constant.
constexpr RegionEntry kRegions[] = {
    {"Americas", 2, RadioConfig::FmBandStart, RadioConfig::FmBandEnd, 2, 10},
    {"Europe / Rest of World", 1, RadioConfig::FmBandStart, RadioConfig::FmBandEnd, 1, 9},
    {"Japan", 1, 7600, 9500, 1, 9},
};
constexpr uint8_t kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);
}  // namespace

void RegionStore::begin() { load(); }

void RegionStore::setIndex(uint8_t index) {
  index_ = index < kRegionCount ? index : 0;
  save();
}

const RegionEntry &RegionStore::current() const { return kRegions[index_]; }

uint8_t RegionStore::count() { return kRegionCount; }
const RegionEntry &RegionStore::entry(uint8_t index) {
  return kRegions[index < kRegionCount ? index : 0];
}

void RegionStore::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kIndexKey, index_);
  prefs.end();
}

void RegionStore::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  uint8_t stored = prefs.getUChar(kIndexKey, 0);
  prefs.end();
  index_ = stored < kRegionCount ? stored : 0;
}

#include "TimeFormatStore.h"

#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "timefmt";
constexpr const char *kKey = "is24h";
}  // namespace

void TimeFormatStore::begin() { load(); }

void TimeFormatStore::toggle() {
  is24Hour_ = !is24Hour_;
  save();
}

void TimeFormatStore::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kKey, is24Hour_ ? 1 : 0);
  prefs.end();
}

void TimeFormatStore::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  is24Hour_ = prefs.getUChar(kKey, 1) != 0;
  prefs.end();
}

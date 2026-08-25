#include "TimezoneStore.h"

#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "timezone";
constexpr const char *kIndexKey = "index";

// POSIX TZ strings, from https://github.com/nayarsystems/posix_tz_db (spot
// check against a canonical source if a listed zone's DST dates look wrong
// -- transition rules occasionally change by local legislation).
constexpr TimezoneEntry kTimezones[] = {
    {"UTC", "UTC0"},
    {"US Eastern (New York)", "EST5EDT,M3.2.0,M11.1.0/2"},
    {"US Central (Chicago)", "CST6CDT,M3.2.0,M11.1.0/2"},
    {"US Mountain (Denver)", "MST7MDT,M3.2.0,M11.1.0/2"},
    {"US Arizona (Phoenix)", "MST7"},
    {"US Pacific (Los Angeles)", "PST8PDT,M3.2.0,M11.1.0/2"},
    {"UK/Ireland (London)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe (Paris/Berlin)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe (Athens)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"India (Mumbai/Delhi)", "IST-5:30"},
    {"China (Beijing)", "CST-8"},
    {"Japan (Tokyo)", "JST-9"},
    {"Australia Eastern (Sydney)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia Western (Perth)", "AWST-8"},
    {"New Zealand (Auckland)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Brazil (Sao Paulo)", "BRT3"},
};
constexpr uint8_t kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);
}  // namespace

void TimezoneStore::begin() { load(); }

void TimezoneStore::setIndex(uint8_t index) {
  index_ = index < kTimezoneCount ? index : 0;
  save();
}

void TimezoneStore::next() { setIndex((index_ + 1) % kTimezoneCount); }
void TimezoneStore::previous() { setIndex((index_ + kTimezoneCount - 1) % kTimezoneCount); }

const char *TimezoneStore::label() const { return kTimezones[index_].label; }
const char *TimezoneStore::posixString() const { return kTimezones[index_].posix; }

uint8_t TimezoneStore::count() { return kTimezoneCount; }
const TimezoneEntry &TimezoneStore::entry(uint8_t index) {
  return kTimezones[index < kTimezoneCount ? index : 0];
}

void TimezoneStore::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kIndexKey, index_);
  prefs.end();
}

void TimezoneStore::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  uint8_t stored = prefs.getUChar(kIndexKey, 0);
  prefs.end();
  index_ = stored < kTimezoneCount ? stored : 0;
}

#pragma once

// Minimal native stand-in for RTClib's DateTime/TimeSpan value types --
// just the subset AlarmClock's logic actually uses. Pulling in the real
// RTClib natively means dragging in Adafruit BusIO (SPI/I2C hardware
// headers) via an Arduino-API mock, which turned out to have real gaps
// (no BitOrder, no global min()) that real Adafruit source code needs.
// DateTime/TimeSpan are themselves pure calendar math with no hardware
// dependency, so it's simpler and more robust to reimplement just that
// here than to keep patching a fake Arduino environment.
//
// Stores everything as a Unix timestamp and derives fields on demand using
// Howard Hinnant's civil_from_days/days_from_civil algorithms
// (https://howardhinnant.github.io/date_algorithms.html) -- correct
// proleptic-Gregorian calendar math, so date/day-of-week rollover behaves
// the same way the real RTClib does. Verified against Python's datetime
// for the dates used in tests.

#include <cstdint>

namespace native_fakes_detail {
inline int64_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);              // [0, 399]
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;    // [0, 365]
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace native_fakes_detail

class TimeSpan {
 public:
  TimeSpan(int32_t seconds = 0) : seconds_(seconds) {}
  TimeSpan(int16_t days, int8_t hours, int8_t minutes, int8_t seconds)
      : seconds_(seconds + 60 * (minutes + 60 * (hours + 24 * static_cast<int32_t>(days)))) {}
  int32_t totalseconds() const { return seconds_; }

 private:
  int32_t seconds_;
};

class DateTime {
 public:
  DateTime() : unixtime_(946684800) {}  // 2000-01-01 00:00:00 UTC, matches RTClib's default
  explicit DateTime(int64_t unixtime) : unixtime_(unixtime) {}
  DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour = 0, uint8_t minute = 0,
           uint8_t second = 0) {
    int64_t days = native_fakes_detail::days_from_civil(year, month, day);
    unixtime_ = days * 86400 + hour * 3600 + minute * 60 + second;
  }

  uint8_t hour() const { return static_cast<uint8_t>((unixtime_ / 3600) % 24); }
  uint8_t minute() const { return static_cast<uint8_t>((unixtime_ / 60) % 60); }
  uint8_t second() const { return static_cast<uint8_t>(unixtime_ % 60); }
  // 0 = Sunday .. 6 = Saturday, matching RTClib's convention.
  uint8_t dayOfTheWeek() const {
    int64_t days = unixtime_ / 86400;
    return static_cast<uint8_t>((days + 4) % 7);
  }
  int64_t unixtime() const { return unixtime_; }

  DateTime operator+(const TimeSpan &span) const { return DateTime(unixtime_ + span.totalseconds()); }

 private:
  int64_t unixtime_;
};

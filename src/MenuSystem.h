#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DebouncedButton.h"
#include "RadioTuner.h"
#include "TimeFormatStore.h"
#include "TimezoneStore.h"

enum class MenuScreen { Home, AlarmList, AlarmEdit, Radio, WifiInfo, SetTime, Timezone };

// Which pictogram renderHome()'s app-icon-style nav row draws in each
// rounded-square badge -- see MenuSystem.cpp's drawAppIcon().
enum class IconGlyph { Bell, Radio, Wifi, Calendar, Globe };

// tft.init(135, 240) + setRotation(3) puts the display in landscape.
constexpr int16_t kMenuScreenWidth = 240;
constexpr int16_t kMenuScreenHeight = 135;

// Renders the on-device menu to the built-in TFT and drives it from the
// three onboard buttons: D1/D2 move the cursor or adjust a value, D0
// selects/confirms (short press) or backs out a level (long press).
class MenuSystem {
 public:
  // rtc may be null (e.g. before the RTC is wired up) -- the Set Time
  // screen still displays but saving silently does nothing.
  MenuSystem(Adafruit_ST7789 &tft, AlarmClock &alarms, RadioTuner &radio, BatteryMonitor *battery,
             RTC_DS3231 *rtc, TimezoneStore &timezone, TimeFormatStore &timeFormat);

  void begin();
  // now: current time for the Home screen and alarm status; wifiStatusLine
  // is a short caller-supplied string (SSID/IP or AP name) for WifiInfo.
  // wifiOnline mirrors WebDashboard::isOnline() -- it's what greys out (and
  // disables) the Date & Time screen's Sync Now row when there's no uplink
  // to sync against. Defaults to true so existing callers/tests that don't
  // care about it don't need updating.
  void update(const DateTime &now, const String &wifiStatusLine, bool wifiOnline = true);

  // rtc is constructed and wired up before rtc->begin() is ever called (it's
  // a global, initialized before setup() runs), so the constructor can't
  // know yet whether the hardware actually responded -- call this once
  // setup() finds out, so a non-null-but-non-functional rtc doesn't get
  // treated as available.
  void setRtcAvailable(bool available) { rtcAvailable_ = available; }

  // True exactly once after the user selects "Sync Now" on the Date & Time
  // screen -- MenuSystem has no reference to WebDashboard (nothing else
  // here needs one), so main.cpp's loop() polls this each iteration and,
  // if set, triggers the actual NTP resync itself.
  bool consumeNtpSyncRequest() {
    bool requested = ntpSyncRequested_;
    ntpSyncRequested_ = false;
    return requested;
  }

 private:
  void handleInput(const DateTime &now);
  void render(const DateTime &now, const String &wifiStatusLine);
  // Faked bold: draws the classic bitmap font twice, offset by one pixel, so
  // strokes overlap and thicken. Used only where digit alignment matters
  // (the Home clock) or a string might be too long for a real font at a
  // legible size (the Timezone screen's label). Leaves canvas_'s cursor at
  // (x, y) (the first draw's position), same as a single print() would.
  void printBold(int16_t x, int16_t y, const char *text);
  // Draws `text` in a real bold proportional font (FreeSansBold9pt7b) with
  // its visual top-left at (x, yTop). Used for every screen's title.
  void printHeader(int16_t x, int16_t yTop, const char *text);
  // Draws `text` in a real regular proportional font (FreeSans9pt7b, dim
  // gray) with its visual top-left at (x, yTop). Used for every screen's
  // footer hint.
  void printHint(int16_t x, int16_t yTop, const char *text);
  // Positions the cursor so text in whatever font/size is currently set
  // will have its visual top at (x, yTop) -- GFXfont draws from the
  // baseline, not the top, so this measures the actual ascent via
  // getTextBounds() rather than guessing a fixed offset per font/size.
  void setCursorTop(int16_t x, int16_t yTop);

  // Lock-screen-style status bar icons (top-right of Home). WiFi draws the
  // classic fan-plus-dot glyph when connected to a home network, or plain
  // "AP" text while still on the setup access point -- trying to cram an
  // "AP" badge onto the tiny fan shape wasn't legible at this size. Battery
  // draws an empty dim outline with no fill/percent when unavailable
  // (no reliable charging signal exists yet -- see BatteryMonitor -- so
  // there's no lightning-bolt state, just outline/fill color).
  void drawWifiStatusIcon(int16_t x, int16_t y, bool staConnected);
  void drawBatteryStatusIcon(int16_t x, int16_t y);
  // Small bell glyph, drawn only while at least one alarm is enabled --
  // absent (not dimmed) otherwise, same "nothing to report" spirit as
  // Battery's unavailable case, but without an empty-outline state since
  // there's no useful "off" shape for a bell the way there is for a
  // battery. Answers the "is anything armed?" question renderHome()'s
  // lock-screen layout otherwise dropped (see its own comment). Turns
  // orange instead of white while snoozing -- see the .cpp for why that's
  // layered on this glyph rather than a separate icon.
  void drawAlarmStatusIcon(int16_t x, int16_t y, bool anyEnabled, bool snoozing);

  // One rounded-square app-icon-style badge for Home's bottom nav row:
  // colored background, a simple glyph, brightened plus a border when
  // focused.
  void drawAppIcon(int16_t x, int16_t y, IconGlyph glyph, uint16_t bgColor, bool focused);

  void renderHome(const DateTime &now);
  void renderAlarmList();
  void renderAlarmEdit();
  void renderRadio();
  void renderWifiInfo(const String &wifiStatusLine);
  void renderSetTime();
  void renderTimezone();

  Adafruit_ST7789 &tft_;
  // Every render draws into this off-screen buffer first, then render()
  // blits the whole finished frame to tft_ in one shot -- drawing straight
  // to tft_ (the previous approach) meant a fillScreen() + incremental
  // redraw was visibly flashing black on every refresh, including the
  // once-a-second live clock tick on Home.
  GFXcanvas16 canvas_{kMenuScreenWidth, kMenuScreenHeight};
  AlarmClock &alarms_;
  RadioTuner &radio_;
  BatteryMonitor *battery_;
  RTC_DS3231 *rtc_;
  bool rtcAvailable_ = true;
  TimezoneStore &timezone_;
  TimeFormatStore &timeFormat_;

  DebouncedButton select_{Pins::MenuSelect};
  // This board's D1/D2 are wired active-high (external pull-down) -- the
  // opposite of D0 and every other button in this project.
  DebouncedButton up_{Pins::MenuUp, /*activeHigh=*/true};
  DebouncedButton down_{Pins::MenuDown, /*activeHigh=*/true};

  MenuScreen screen_ = MenuScreen::Home;
  // Home: index into {AlarmList, Radio, WifiInfo, SetTime, Timezone}.
  // AlarmList: alarm index.
  uint8_t cursor_ = 0;
  // Which row is selected: AlarmEdit (0-5) and SetTime (0-7) both reuse
  // this, since the two screens are never active at the same time.
  uint8_t editField_ = 0;
  Alarm editingAlarm_;  // working copy while in AlarmEdit, until saved
  // Working copies while in SetTime (fields: Year, Month, Day, Hour,
  // Minute, Format, Sync Now, Save), until saved. Day-of-week isn't itself
  // editable -- it's always derived from the date (RTClib's
  // DateTime::dayOfTheWeek()), so fixing the date here is what keeps it
  // correct.
  uint16_t editingYear_ = 2026;
  uint8_t editingMonth_ = 1;   // 1-12
  uint8_t editingDay_ = 1;     // 1-31, clamped to the actual month length
  uint8_t editingHour_ = 0;
  uint8_t editingMinute_ = 0;
  bool wifiOnline_ = true;         // see update()'s wifiOnline parameter
  bool ntpSyncRequested_ = false;  // see consumeNtpSyncRequest()
  bool dirty_ = true;              // forces a redraw on the next update()

  uint32_t selectPressedAtMs_ = 0;
  // Set the instant a long press fires (while select_ is still held down),
  // and only cleared by the next fresh justPressed() -- not by release.
  // Without this latch, a screen/state change made in response to the long
  // press (e.g. backing out to a different screen) would still see
  // select_.isDown() == true on every following handleInput() call until
  // the user's finger actually lifts, which would either re-fire the long
  // press instantly on the new screen or (worse) get misread as a fresh
  // press on it the moment they let go.
  bool longPressFired_ = false;
  static constexpr uint16_t kLongPressMs = 1000;

  // Radio screen: a tap steps by one FmStep; holding past kLongPressMs
  // without releasing starts a station seek instead, exactly once per hold
  // rather than repeating (deliberately not auto-repeating a step here --
  // see handleInput()). upSeekFired_/downSeekFired_ latch true the moment
  // that happens (same shape as longPressFired_ above) so a still-held
  // button doesn't keep re-seeking once the hold has already fired one.
  // The seek itself runs in RadioTuner (advanced from main.cpp's fast
  // path); renderRadio() reads radio_.seeking() to show "Seeking...".
  uint32_t upPressedAtMs_ = 0;
  uint32_t downPressedAtMs_ = 0;
  bool upSeekFired_ = false;
  bool downSeekFired_ = false;
};

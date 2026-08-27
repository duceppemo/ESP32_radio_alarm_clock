#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DebouncedButton.h"
#include "RadioTuner.h"
#include "TimezoneStore.h"

enum class MenuScreen { Home, AlarmList, AlarmEdit, Radio, WifiInfo, SetTime, Timezone };

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
             RTC_DS3231 *rtc, TimezoneStore &timezone);

  void begin();
  // now: current time for the Home screen and alarm status; wifiStatusLine
  // is a short caller-supplied string (SSID/IP or AP name) for WifiInfo.
  void update(const DateTime &now, const String &wifiStatusLine);

  // rtc is constructed and wired up before rtc->begin() is ever called (it's
  // a global, initialized before setup() runs), so the constructor can't
  // know yet whether the hardware actually responded -- call this once
  // setup() finds out, so a non-null-but-non-functional rtc doesn't get
  // treated as available.
  void setRtcAvailable(bool available) { rtcAvailable_ = available; }

 private:
  void handleInput(const DateTime &now);
  void render(const DateTime &now, const String &wifiStatusLine);
  // Faked bold: draws the classic bitmap font twice, offset by one pixel, so
  // strokes overlap and thicken. Adafruit_GFX's built-in font has no bold
  // weight of its own. Leaves canvas_'s cursor at (x, y) (the first draw's
  // position), same as a single print() would.
  void printBold(int16_t x, int16_t y, const char *text);

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

  DebouncedButton select_{Pins::MenuSelect};
  // This board's D1/D2 are wired active-high (external pull-down) -- the
  // opposite of D0 and every other button in this project.
  DebouncedButton up_{Pins::MenuUp, /*activeHigh=*/true};
  DebouncedButton down_{Pins::MenuDown, /*activeHigh=*/true};

  MenuScreen screen_ = MenuScreen::Home;
  // Home: index into {AlarmList, Radio, WifiInfo, SetTime, Timezone}.
  // AlarmList: alarm index.
  uint8_t cursor_ = 0;
  // Which row is selected: AlarmEdit (0-5) and SetTime (0-2) both reuse this,
  // since the two screens are never active at the same time.
  uint8_t editField_ = 0;
  Alarm editingAlarm_;      // working copy while in AlarmEdit, until saved
  uint8_t editingHour_ = 0;    // working copy while in SetTime, until saved
  uint8_t editingMinute_ = 0;  // working copy while in SetTime, until saved
  bool dirty_ = true;       // forces a redraw on the next update()

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
};

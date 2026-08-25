#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DebouncedButton.h"
#include "RadioTuner.h"

enum class MenuScreen { Home, AlarmList, AlarmEdit, Radio, WifiInfo };

// Renders the on-device menu to the built-in TFT and drives it from the
// three onboard buttons: D1/D2 move the cursor or adjust a value, D0
// selects/confirms (short press) or backs out a level (long press).
class MenuSystem {
 public:
  MenuSystem(Adafruit_ST7789 &tft, AlarmClock &alarms, RadioTuner &radio, BatteryMonitor *battery);

  void begin();
  // now: current time for the Home screen and alarm status; wifiStatusLine
  // is a short caller-supplied string (SSID/IP or AP name) for WifiInfo.
  void update(const DateTime &now, const String &wifiStatusLine);

 private:
  void handleInput(const DateTime &now);
  void render(const DateTime &now, const String &wifiStatusLine);

  void renderHome(const DateTime &now);
  void renderAlarmList();
  void renderAlarmEdit();
  void renderRadio();
  void renderWifiInfo(const String &wifiStatusLine);

  Adafruit_ST7789 &tft_;
  AlarmClock &alarms_;
  RadioTuner &radio_;
  BatteryMonitor *battery_;

  DebouncedButton select_{Pins::MenuSelect};
  DebouncedButton up_{Pins::MenuUp};
  DebouncedButton down_{Pins::MenuDown};

  MenuScreen screen_ = MenuScreen::Home;
  // Home: index into {AlarmList, Radio, WifiInfo}. AlarmList: alarm index.
  uint8_t cursor_ = 0;
  uint8_t editField_ = 0;  // which row is selected in AlarmEdit (0-5)
  Alarm editingAlarm_;     // working copy while in AlarmEdit, until saved
  bool dirty_ = true;      // forces a redraw on the next update()

  uint32_t selectPressedAtMs_ = 0;
  static constexpr uint16_t kLongPressMs = 600;
};

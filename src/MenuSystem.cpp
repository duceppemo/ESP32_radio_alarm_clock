#include "MenuSystem.h"

namespace {
constexpr uint8_t kDaysWeekdays = 0b0111110;
constexpr uint8_t kDaysWeekends = 0b1000001;
constexpr uint8_t kDaysEveryday = 0b1111111;

const char *daysLabel(uint8_t mask) {
  if (mask == kDaysWeekdays) return "Weekdays";
  if (mask == kDaysWeekends) return "Weekends";
  if (mask == kDaysEveryday) return "Every day";
  return "Custom";
}

uint8_t cycleDays(uint8_t mask, int8_t direction) {
  static const uint8_t presets[] = {kDaysWeekdays, kDaysWeekends, kDaysEveryday};
  constexpr uint8_t n = sizeof(presets) / sizeof(presets[0]);
  uint8_t index = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (presets[i] == mask) index = i;
  }
  index = (index + direction + n) % n;
  return presets[index];
}

const char *wakeSourceLabel(WakeSource source) {
  switch (source) {
    case WakeSource::ClassicBeep:
      return "Beep";
    case WakeSource::Chime:
      return "Chime";
    default:
      return "Radio";
  }
}

WakeSource cycleWakeSource(WakeSource source, int8_t direction) {
  int8_t next = (static_cast<int8_t>(source) + direction + 3) % 3;
  return static_cast<WakeSource>(next);
}

constexpr uint8_t kHomeMenuItems = 3;  // AlarmList, Radio, WifiInfo
}  // namespace

MenuSystem::MenuSystem(Adafruit_ST7789 &tft, AlarmClock &alarms, RadioTuner &radio,
                       BatteryMonitor *battery)
    : tft_(tft), alarms_(alarms), radio_(radio), battery_(battery) {}

void MenuSystem::begin() {
  pinMode(Pins::MenuSelect, INPUT_PULLUP);
  pinMode(Pins::MenuUp, INPUT_PULLUP);
  pinMode(Pins::MenuDown, INPUT_PULLUP);
}

void MenuSystem::update(const DateTime &now, const String &wifiStatusLine) {
  select_.update();
  up_.update();
  down_.update();

  handleInput(now);
  render(now, wifiStatusLine);
}

void MenuSystem::handleInput(const DateTime &now) {
  if (select_.justPressed()) selectPressedAtMs_ = millis();

  bool longPress = false;
  bool shortPress = false;
  if (select_.justReleased()) {
    uint32_t heldMs = millis() - selectPressedAtMs_;
    if (heldMs >= kLongPressMs) {
      longPress = true;
    } else {
      shortPress = true;
    }
  }

  bool up = up_.justPressed();
  bool down = down_.justPressed();
  if (!up && !down && !shortPress && !longPress) return;
  dirty_ = true;

  switch (screen_) {
    case MenuScreen::Home: {
      if (alarms_.state() != AlarmState::Idle) {
        if (shortPress) alarms_.snooze(now);
        if (longPress) alarms_.dismiss();
        return;
      }
      if (up) cursor_ = (cursor_ + kHomeMenuItems - 1) % kHomeMenuItems;
      if (down) cursor_ = (cursor_ + 1) % kHomeMenuItems;
      if (shortPress) {
        switch (cursor_) {
          case 0:
            screen_ = MenuScreen::AlarmList;
            cursor_ = 0;
            break;
          case 1:
            screen_ = MenuScreen::Radio;
            break;
          case 2:
            screen_ = MenuScreen::WifiInfo;
            break;
        }
      }
      break;
    }

    case MenuScreen::AlarmList: {
      if (up) cursor_ = (cursor_ + AlarmClock::count() - 1) % AlarmClock::count();
      if (down) cursor_ = (cursor_ + 1) % AlarmClock::count();
      if (shortPress) {
        editingAlarm_ = alarms_.alarm(cursor_);
        editField_ = 0;
        screen_ = MenuScreen::AlarmEdit;
      }
      if (longPress) {
        screen_ = MenuScreen::Home;
        cursor_ = 0;
      }
      break;
    }

    case MenuScreen::AlarmEdit: {
      if (up || down) {
        int8_t dir = up ? 1 : -1;
        switch (editField_) {
          case 0:
            editingAlarm_.enabled = !editingAlarm_.enabled;
            break;
          case 1:
            editingAlarm_.hour = (editingAlarm_.hour + 24 + dir) % 24;
            break;
          case 2:
            editingAlarm_.minute = (editingAlarm_.minute + 60 + dir) % 60;
            break;
          case 3:
            editingAlarm_.daysMask = cycleDays(editingAlarm_.daysMask, dir);
            break;
          case 4:
            editingAlarm_.wakeSource = cycleWakeSource(editingAlarm_.wakeSource, dir);
            break;
        }
      }
      if (shortPress) {
        if (editField_ >= 5) {
          alarms_.setAlarm(cursor_, editingAlarm_);
          screen_ = MenuScreen::AlarmList;
        } else {
          editField_++;
        }
      }
      if (longPress) {
        screen_ = MenuScreen::AlarmList;  // discard edits
      }
      break;
    }

    case MenuScreen::Radio: {
      if (up) radio_.tune(radio_.frequency10kHz() + RadioConfig::FmStep);
      if (down) radio_.tune(radio_.frequency10kHz() - RadioConfig::FmStep);
      if (shortPress) radio_.setMuted(!radio_.muted());
      if (longPress) screen_ = MenuScreen::Home;
      break;
    }

    case MenuScreen::WifiInfo: {
      if (longPress) screen_ = MenuScreen::Home;
      break;
    }
  }
}

void MenuSystem::render(const DateTime &now, const String &wifiStatusLine) {
  static uint32_t lastClockRedrawSec = 255;  // Home redraws every tick to show live time
  bool isHomeClock = screen_ == MenuScreen::Home && alarms_.state() == AlarmState::Idle;
  if (!dirty_ && !(isHomeClock && now.second() != lastClockRedrawSec)) return;
  dirty_ = false;
  lastClockRedrawSec = now.second();

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setCursor(0, 0);
  tft_.setTextColor(ST77XX_WHITE);
  tft_.setTextSize(1);

  switch (screen_) {
    case MenuScreen::Home:
      renderHome(now);
      break;
    case MenuScreen::AlarmList:
      renderAlarmList();
      break;
    case MenuScreen::AlarmEdit:
      renderAlarmEdit();
      break;
    case MenuScreen::Radio:
      renderRadio();
      break;
    case MenuScreen::WifiInfo:
      renderWifiInfo(wifiStatusLine);
      break;
  }
}

void MenuSystem::renderHome(const DateTime &now) {
  if (alarms_.state() != AlarmState::Idle) {
    tft_.setTextColor(ST77XX_RED);
    tft_.println("** ALARM **");
    tft_.setTextColor(ST77XX_WHITE);
    if (alarms_.ringingAlarmIndex() >= 0) {
      const Alarm &a = alarms_.alarm(alarms_.ringingAlarmIndex());
      tft_.printf("%02d:%02d\n\n", a.hour, a.minute);
    }
    tft_.println(alarms_.state() == AlarmState::Snoozed ? "Snoozed" : "Ringing");
    tft_.println();
    tft_.println("tap: snooze");
    tft_.println("hold: dismiss");
    return;
  }

  tft_.setTextSize(2);
  tft_.printf("%02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
  tft_.setTextSize(1);
  tft_.println();

  bool anyEnabled = false;
  for (uint8_t i = 0; i < AlarmClock::count(); i++) {
    if (alarms_.alarm(i).enabled) anyEnabled = true;
  }
  tft_.println(anyEnabled ? "Alarms set" : "No alarms set");
  tft_.printf("FM %.1f MHz\n", radio_.frequencyMHz());
  if (battery_ && battery_->available()) {
    if (battery_->isLow()) tft_.setTextColor(ST77XX_RED);
    tft_.printf("Battery: %.0f%%\n", battery_->percent());
    tft_.setTextColor(ST77XX_WHITE);
  }
  tft_.println();

  static const char *items[kHomeMenuItems] = {"Alarms", "Radio", "WiFi"};
  for (uint8_t i = 0; i < kHomeMenuItems; i++) {
    tft_.printf("%s%s  ", i == cursor_ ? ">" : " ", items[i]);
  }
  tft_.println();
}

void MenuSystem::renderAlarmList() {
  tft_.println("Alarms");
  tft_.println();
  for (uint8_t i = 0; i < AlarmClock::count(); i++) {
    const Alarm &a = alarms_.alarm(i);
    tft_.printf("%s%02d:%02d %-3s %s\n", i == cursor_ ? ">" : " ", a.hour, a.minute,
                a.enabled ? "ON" : "off", daysLabel(a.daysMask));
  }
  tft_.println();
  tft_.println("tap: edit  hold: back");
}

void MenuSystem::renderAlarmEdit() {
  tft_.printf("Edit alarm %u\n\n", cursor_ + 1);

  static const char *rows[] = {"Enabled", "Hour", "Minute", "Days", "Wake", "Save"};
  for (uint8_t i = 0; i < 6; i++) {
    tft_.print(i == editField_ ? "> " : "  ");
    tft_.print(rows[i]);
    switch (i) {
      case 0:
        tft_.println(editingAlarm_.enabled ? ": On" : ": Off");
        break;
      case 1:
        tft_.printf(": %02d\n", editingAlarm_.hour);
        break;
      case 2:
        tft_.printf(": %02d\n", editingAlarm_.minute);
        break;
      case 3:
        tft_.print(": ");
        tft_.println(daysLabel(editingAlarm_.daysMask));
        break;
      case 4:
        tft_.print(": ");
        tft_.println(wakeSourceLabel(editingAlarm_.wakeSource));
        break;
      default:
        tft_.println();
    }
  }
  tft_.println();
  tft_.println("tap: next  hold: cancel");
}

void MenuSystem::renderRadio() {
  tft_.println("Radio");
  tft_.println();
  tft_.setTextSize(2);
  tft_.printf("%.1f MHz\n", radio_.frequencyMHz());
  tft_.setTextSize(1);
  tft_.println();
  tft_.printf("Signal: %u\n", radio_.rssi());
  tft_.printf("Volume: %u\n", radio_.volume());
  tft_.println(radio_.muted() ? "Muted" : "Unmuted");
  if (radio_.sleepTimerActive()) {
    tft_.printf("Sleep: %um\n", radio_.sleepTimerRemainingMinutes());
  }
  tft_.println();
  tft_.println("up/down: tune");
  tft_.println("tap: mute  hold: back");
}

void MenuSystem::renderWifiInfo(const String &wifiStatusLine) {
  tft_.println("WiFi");
  tft_.println();
  tft_.println(wifiStatusLine);
  tft_.println();
  tft_.println("hold: back");
}

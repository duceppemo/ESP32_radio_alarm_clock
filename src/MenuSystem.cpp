#include "MenuSystem.h"

#include <cstring>

#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

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

// The DS3231 (and RTClib's DateTime) only really cover 2000-2099 (2-digit
// year register + a fixed century), so Year wraps within that range rather
// than letting it drift to something the RTC can't actually store.
uint16_t cycleYear(uint16_t year, int8_t direction) {
  return 2000 + (uint16_t)((year - 2000 + 100 + direction) % 100);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap) return 29;
  return kDays[month - 1];
}

const char *monthName(uint8_t month) {
  static const char *kNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return kNames[month - 1];
}

// RTClib's DateTime::dayOfTheWeek() returns 0=Sunday.
const char *dayOfWeekName(uint8_t dow) {
  static const char *kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return kNames[dow];
}

constexpr uint8_t kHomeMenuItems = 5;  // AlarmList, Radio, WifiInfo, SetTime, Timezone

// Dim gray for de-emphasized text (unselected menu items, "no radio" etc.) --
// not one of Adafruit_ST77xx's named colors, so computed directly (RGB565,
// ~mid-gray).
constexpr uint16_t kDimGray = 0x8410;
// Soft sky-blue accent, used sparingly (currently just the Home date line)
// to give the screen a second focal color beyond white/dim-gray plus the
// functional red/orange/green already used for alerts and status.
constexpr uint16_t kAccentBlue = 0x5C3F;

// App-icon badge background colors for Home's bottom nav row, chosen to
// echo (not match pixel-for-pixel) the iOS system colors for the app each
// stands in for -- Clock's orange, a Music/Podcasts-style purple, Wi-Fi
// blue, Calendar red, and a globe green.
constexpr uint16_t kIconOrange = 0xFD20;  // == ST77XX_ORANGE
constexpr uint16_t kIconPurple = 0xAA9B;
constexpr uint16_t kIconBlue = 0x0C3F;
constexpr uint16_t kIconRed = 0xF800;  // == ST77XX_RED
constexpr uint16_t kIconGreen = 0x368B;

// Top-right corner placement shared by every screen that echoes its Home
// nav icon next to its header, as a small unfocused (borderless) badge.
constexpr int16_t kHeaderIconX = 216;
constexpr int16_t kHeaderIconY = 2;
}  // namespace

void MenuSystem::printBold(int16_t x, int16_t y, const char *text) {
  canvas_.setCursor(x + 1, y);
  canvas_.print(text);
  canvas_.setCursor(x, y);
  canvas_.print(text);
}

void MenuSystem::setCursorTop(int16_t x, int16_t yTop) {
  int16_t x1, y1;
  uint16_t w, h;
  // A representative sample rather than the real string: what matters is
  // the font's ascent (how far above the baseline its tallest glyphs
  // reach), which "Hg" captures (cap height + descender) regardless of
  // what text is about to be printed.
  canvas_.getTextBounds("Hg", 0, 0, &x1, &y1, &w, &h);
  canvas_.setCursor(x, yTop - y1);
}

void MenuSystem::printHeader(int16_t x, int16_t yTop, const char *text) {
  // GFXfonts are already sized at their own nominal point size -- reset to
  // an unscaled size 1 first, or whatever integer scale was left over from
  // classic-font content elsewhere on the screen would multiply it too.
  canvas_.setTextSize(1);
  canvas_.setFont(&FreeSansBold9pt7b);
  setCursorTop(x, yTop);
  canvas_.print(text);
  canvas_.setFont(nullptr);
}

void MenuSystem::printHint(int16_t x, int16_t yTop, const char *text) {
  canvas_.setTextSize(1);
  canvas_.setFont(&FreeSans9pt7b);
  setCursorTop(x, yTop);
  canvas_.setTextColor(kDimGray);
  canvas_.print(text);
  canvas_.setFont(nullptr);
}

MenuSystem::MenuSystem(Adafruit_ST7789 &tft, AlarmClock &alarms, RadioTuner &radio,
                       BatteryMonitor *battery, RTC_DS3231 *rtc, TimezoneStore &timezone,
                       TimeFormatStore &timeFormat)
    : tft_(tft),
      alarms_(alarms),
      radio_(radio),
      battery_(battery),
      rtc_(rtc),
      timezone_(timezone),
      timeFormat_(timeFormat) {}

void MenuSystem::begin() {
  pinMode(Pins::MenuSelect, INPUT_PULLUP);
  // D1/D2 read HIGH when pressed (see DebouncedButton's activeHigh for the
  // read side) and need a pull-down at rest. They don't reliably have one
  // on this board on their own -- plain INPUT left them floating, which
  // picked up and held spurious HIGH readings once another I2C device
  // (the 7-segment display) was added to the bus. INPUT_PULLDOWN enables
  // the ESP32's own internal pull-down instead of depending on the board.
  pinMode(Pins::MenuUp, INPUT_PULLDOWN);
  pinMode(Pins::MenuDown, INPUT_PULLDOWN);
}

void MenuSystem::update(const DateTime &now, const String &wifiStatusLine, bool wifiOnline) {
  wifiOnline_ = wifiOnline;
  select_.update();
  up_.update();
  down_.update();

  handleInput(now);
  render(now, wifiStatusLine);
}

void MenuSystem::handleInput(const DateTime &now) {
  if (select_.justPressed()) {
    selectPressedAtMs_ = millis();
    longPressFired_ = false;
  }

  bool longPress = false;
  bool shortPress = false;
  // Fires the instant the hold crosses the threshold, while the button is
  // still down -- not on release -- so it's not confusing whether it
  // registered. longPressFired_ latches until the next fresh press so it
  // can only happen once per hold, and so the short-press branch below
  // never also fires when the same hold is finally released.
  if (select_.isDown() && !longPressFired_ && millis() - selectPressedAtMs_ >= kLongPressMs) {
    longPress = true;
    longPressFired_ = true;
  } else if (select_.justReleased() && !longPressFired_) {
    shortPress = true;
  }

  // Two flavors of up/down: a plain tap for moving a cursor through a short
  // list (Home, AlarmList) or changing a value by one step (Radio), and an
  // auto-repeating one for adjusting a value continuously (AlarmEdit
  // fields, Set Time, Timezone) so holding the button keeps changing it
  // instead of needing repeated taps. triggered() has a side effect
  // (advances its own repeat schedule) so it's computed exactly once here
  // per button and reused below, never called again.
  bool upTap = up_.justPressed();
  bool downTap = down_.justPressed();
  bool upRepeat = up_.triggered();
  bool downRepeat = down_.triggered();

  // Radio screen only: holding past kLongPressMs seeks instead of taking
  // repeated taps to step through frequencies one at a time -- deliberately
  // NOT using upRepeat/downRepeat above, so holding never auto-repeats a
  // step; it's either a tap (one step) or a long hold (one seek), nothing
  // in between. See upSeekFired_/downSeekFired_'s comment in MenuSystem.h
  // for why each latches once it fires.
  if (upTap) {
    upPressedAtMs_ = millis();
    upSeekFired_ = false;
  }
  if (downTap) {
    downPressedAtMs_ = millis();
    downSeekFired_ = false;
  }
  bool upLongHold = up_.isDown() && !upSeekFired_ && millis() - upPressedAtMs_ >= kLongPressMs;
  bool downLongHold = down_.isDown() && !downSeekFired_ && millis() - downPressedAtMs_ >= kLongPressMs;
  if (upLongHold) upSeekFired_ = true;
  if (downLongHold) downSeekFired_ = true;

  // upLongHold/downLongHold must be in this guard too -- without it, the
  // tick that first crosses kLongPressMs could return early before ever
  // reaching the Radio case below (upRepeat/downRepeat run on their own
  // 150ms schedule, independent of the long-press threshold, so they often
  // aren't also true on that exact tick). upSeekFired_ latches true just
  // above regardless, so a long press would silently do nothing at all --
  // indistinguishable from a plain tap having already fired its one step.
  if (!upTap && !downTap && !upRepeat && !downRepeat && !shortPress && !longPress && !upLongHold &&
      !downLongHold) {
    return;
  }
  dirty_ = true;

  switch (screen_) {
    case MenuScreen::Home: {
      if (alarms_.state() != AlarmState::Idle) {
        if (shortPress) alarms_.snooze(now);
        if (longPress) alarms_.dismiss();
        return;
      }
      if (upTap) cursor_ = (cursor_ + kHomeMenuItems - 1) % kHomeMenuItems;
      if (downTap) cursor_ = (cursor_ + 1) % kHomeMenuItems;
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
          case 3:
            editingYear_ = now.year();
            editingMonth_ = now.month();
            editingDay_ = now.day();
            editingHour_ = now.hour();
            editingMinute_ = now.minute();
            editField_ = 0;
            screen_ = MenuScreen::SetTime;
            break;
          case 4:
            screen_ = MenuScreen::Timezone;
            break;
        }
      }
      break;
    }

    case MenuScreen::AlarmList: {
      if (upTap) cursor_ = (cursor_ + AlarmClock::count() - 1) % AlarmClock::count();
      if (downTap) cursor_ = (cursor_ + 1) % AlarmClock::count();
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
      if (upRepeat || downRepeat) {
        int8_t dir = upRepeat ? 1 : -1;
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
      if (radio_.available()) {
        // seekUp()/seekDown() block for up to a couple of seconds -- paint
        // "Seeking..." right now, before making that call, or the screen
        // would just look frozen for that whole time. wifiStatusLine is
        // irrelevant here (renderRadio() never reads it), so "" is fine.
        // dirty_ is forced true afterward so the render() call already
        // scheduled after handleInput() returns still redraws the actual
        // result -- this manual one already consumed dirty_ itself.
        if (upLongHold) {
          seeking_ = true;
          render(now, "");
          radio_.seekUp();
          seeking_ = false;
          dirty_ = true;
        } else if (upTap) {
          radio_.stepUp();
        }
        if (downLongHold) {
          seeking_ = true;
          render(now, "");
          radio_.seekDown();
          seeking_ = false;
          dirty_ = true;
        } else if (downTap) {
          radio_.stepDown();
        }
        if (shortPress) radio_.setMuted(!radio_.muted());
      }
      if (longPress) screen_ = MenuScreen::Home;
      break;
    }

    case MenuScreen::WifiInfo: {
      if (longPress) screen_ = MenuScreen::Home;
      break;
    }

    case MenuScreen::SetTime: {
      // Fields: 0=Year, 1=Month, 2=Day, 3=Hour, 4=Minute, 5=Format (24h/
      // 12h), 6=Sync Now, 7=Save. Sync Now fires on up/down (like Format
      // toggling, or any value field changing) rather than on tap -- tap
      // always just advances to the next field here, uniformly, all the
      // way to Save. That keeps Save reachable with a plain tap-through
      // regardless of whether the user touches Sync Now, and means
      // greying it out when offline is simply "up/down does nothing."
      if (upRepeat || downRepeat) {
        int8_t dir = upRepeat ? 1 : -1;
        switch (editField_) {
          case 0:
            editingYear_ = cycleYear(editingYear_, dir);
            break;
          case 1:
            editingMonth_ = (uint8_t)(((editingMonth_ - 1 + 12 + dir) % 12) + 1);
            break;
          case 2:
            editingDay_ = (uint8_t)(((editingDay_ - 1 + 31 + dir) % 31) + 1);
            break;
          case 3:
            editingHour_ = (editingHour_ + 24 + dir) % 24;
            break;
          case 4:
            editingMinute_ = (editingMinute_ + 60 + dir) % 60;
            break;
          case 5:
            timeFormat_.toggle();
            break;
          case 6:
            if (wifiOnline_) {
              // Actual sync happens in main.cpp's loop() -- MenuSystem has
              // no reference to WebDashboard, so this just raises a flag
              // it polls (see consumeNtpSyncRequest()). Jumping back to
              // Home gives visible feedback once it lands: the live clock
              // updates.
              ntpSyncRequested_ = true;
              screen_ = MenuScreen::Home;
            }
            break;
        }
        // Changing the year or month can leave Day pointing past the end
        // of the now-selected month (e.g. Jan 31 -> Feb) -- clamp rather
        // than let it silently overflow into the following month on save.
        uint8_t maxDay = daysInMonth(editingYear_, editingMonth_);
        if (editingDay_ > maxDay) editingDay_ = maxDay;
      }
      if (shortPress) {
        if (editField_ == 7) {
          if (rtc_ && rtcAvailable_) {
            rtc_->adjust(DateTime(editingYear_, editingMonth_, editingDay_, editingHour_,
                                  editingMinute_, 0));
          }
          screen_ = MenuScreen::Home;
        } else {
          editField_++;
        }
      }
      if (longPress) {
        screen_ = MenuScreen::Home;  // discard
      }
      break;
    }

    case MenuScreen::Timezone: {
      if (upRepeat) timezone_.next();
      if (downRepeat) timezone_.previous();
      if (longPress) screen_ = MenuScreen::Home;
      break;
    }
  }
}

void MenuSystem::render(const DateTime &now, const String &wifiStatusLine) {
  // Home's lock-screen-style clock only shows H:MM (no seconds), so it only
  // needs to redraw once a minute rather than every tick. Snoozed now uses
  // this same lock-screen layout (see renderHome()), so it gets the same
  // treatment; Ringing still fully redraws on every dirty_ instead (its
  // "Ringing"/hint text is static, so per-minute ticking buys it nothing).
  static uint32_t lastClockRedrawMin = 61;
  bool isHomeClock = screen_ == MenuScreen::Home && alarms_.state() != AlarmState::Ringing;

  // Volume Up/Down and the snooze button's sleep-timer toggle are read
  // directly in main.cpp's loop(), never through handleInput() -- so they
  // never set dirty_. Without this, the Radio screen's Vol/Sleep lines
  // would freeze on whatever they last drew even though the hardware
  // (speaker, sleep timer) keeps responding for real. All three reads here
  // are plain member/millis() reads, not I2C, so polling them every fast-
  // path tick is free.
  static uint8_t lastRadioVolume = 0;
  static bool lastRadioMuted = false;
  static uint16_t lastRadioSleepMinutes = 0;
  bool isRadioScreen = screen_ == MenuScreen::Radio && radio_.available();
  bool radioLive = isRadioScreen && (radio_.volume() != lastRadioVolume ||
                                      radio_.muted() != lastRadioMuted ||
                                      radio_.sleepTimerRemainingMinutes() != lastRadioSleepMinutes);

  // Sig/SNR are real I2C queries (RadioTuner::rssi()/snr()), unlike the
  // three above -- polling them every fast-path tick would hammer the bus
  // for no visible benefit, but only refreshing them when something else
  // also changed left them looking frozen between button presses. A plain
  // timer, independent of any button, is what makes them read live.
  static uint32_t lastRadioSignalRefreshMs = 0;
  constexpr uint32_t kRadioSignalRefreshMs = 500;
  bool radioSignalDue = isRadioScreen && !seeking_ &&
                         millis() - lastRadioSignalRefreshMs >= kRadioSignalRefreshMs;

  if (!dirty_ && !(isHomeClock && now.minute() != lastClockRedrawMin) && !radioLive &&
      !radioSignalDue) {
    return;
  }
  dirty_ = false;
  lastClockRedrawMin = now.minute();
  if (isRadioScreen) {
    lastRadioVolume = radio_.volume();
    lastRadioMuted = radio_.muted();
    lastRadioSleepMinutes = radio_.sleepTimerRemainingMinutes();
  }
  if (radioSignalDue) lastRadioSignalRefreshMs = millis();

  canvas_.fillScreen(ST77XX_BLACK);
  canvas_.setCursor(0, 0);
  canvas_.setTextColor(ST77XX_WHITE);
  canvas_.setTextSize(1);

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
    case MenuScreen::SetTime:
      renderSetTime();
      break;
    case MenuScreen::Timezone:
      renderTimezone();
      break;
  }

  // One SPI transfer of the finished frame -- the display never shows an
  // intermediate blank/partial state, so no flash.
  tft_.drawRGBBitmap(0, 0, canvas_.getBuffer(), kMenuScreenWidth, kMenuScreenHeight);
}

void MenuSystem::drawWifiStatusIcon(int16_t x, int16_t y, bool staConnected) {
  if (!staConnected) {
    canvas_.setTextSize(1);
    canvas_.setFont(nullptr);
    canvas_.setTextColor(kDimGray);
    canvas_.setCursor(x, y + 2);
    canvas_.print("AP");
    return;
  }
  // Classic fan-plus-dot glyph: a small dot with two upward-opening arcs
  // above it (the upper-left + upper-right quadrant mask on drawCircleHelper
  // gives the "opening upward" half).
  int16_t cx = x + 7;
  int16_t cy = y + 10;
  canvas_.fillCircle(cx, cy, 1, ST77XX_WHITE);
  canvas_.drawCircleHelper(cx, cy, 4, 0x03, ST77XX_WHITE);
  canvas_.drawCircleHelper(cx, cy, 7, 0x03, ST77XX_WHITE);
}

void MenuSystem::drawBatteryStatusIcon(int16_t x, int16_t y) {
  constexpr int16_t kWidth = 20;
  constexpr int16_t kHeight = 10;
  bool available = battery_ && battery_->available();
  uint16_t outline = available ? ST77XX_WHITE : kDimGray;
  canvas_.drawRoundRect(x, y, kWidth, kHeight, 2, outline);
  canvas_.fillRect(x + kWidth, y + 3, 2, kHeight - 6, outline);  // terminal nub

  if (!available) return;  // empty outline only -- no reliable charge to show

  uint16_t fillColor = battery_->isLow() ? ST77XX_RED : ST77XX_WHITE;
  constexpr int16_t kPad = 2;
  int16_t innerW = kWidth - kPad * 2;
  int16_t fillW = (int16_t)((battery_->percent() / 100.0f) * innerW);
  if (fillW > 0) canvas_.fillRect(x + kPad, y + kPad, fillW, kHeight - kPad * 2, fillColor);

  // Percent, right-aligned just to the left of the icon -- classic font is
  // an exact 6px/char, so this doesn't need a getTextBounds() round-trip.
  char pctBuf[5];
  snprintf(pctBuf, sizeof(pctBuf), "%.0f", battery_->percent());
  int16_t textW = (int16_t)strlen(pctBuf) * 6;
  canvas_.setTextSize(1);
  canvas_.setFont(nullptr);
  canvas_.setTextColor(ST77XX_WHITE);
  canvas_.setCursor(x - textW - 3, y + 1);
  canvas_.print(pctBuf);
}

void MenuSystem::drawAlarmStatusIcon(int16_t x, int16_t y, bool anyEnabled, bool snoozing) {
  if (!anyEnabled) return;

  // Orange while a snooze is in effect (an alarm can only be snoozing if
  // one was enabled and rang, so this is layered on the same glyph rather
  // than needing separate real estate) -- same reused-glyph-recolored
  // pattern as the battery icon's low-charge red and the Radio screen's
  // on-air icon.
  uint16_t color = snoozing ? ST77XX_ORANGE : ST77XX_WHITE;
  int16_t cx = x + 6;
  int16_t cy = y + 6;
  canvas_.fillCircle(cx, cy - 1, 3, color);
  canvas_.fillRect(cx - 3, cy - 1, 6, 2, color);
  canvas_.drawFastHLine(cx - 4, cy + 1, 8, color);
  canvas_.fillCircle(cx, cy + 4, 1, color);
}

void MenuSystem::drawAppIcon(int16_t x, int16_t y, IconGlyph glyph, uint16_t bgColor, bool focused) {
  constexpr int16_t kSize = 20;
  canvas_.fillRoundRect(x, y, kSize, kSize, 5, bgColor);
  if (focused) canvas_.drawRoundRect(x - 1, y - 1, kSize + 2, kSize + 2, 6, ST77XX_WHITE);

  int16_t cx = x + kSize / 2;
  int16_t cy = y + kSize / 2;
  constexpr uint16_t kGlyphColor = ST77XX_WHITE;

  switch (glyph) {
    case IconGlyph::Bell:
      canvas_.fillCircle(cx, cy - 1, 5, kGlyphColor);
      canvas_.fillRect(cx - 5, cy - 1, 10, 3, kGlyphColor);
      canvas_.drawFastHLine(cx - 6, cy + 3, 12, kGlyphColor);
      canvas_.fillCircle(cx, cy + 6, 2, kGlyphColor);
      break;
    case IconGlyph::Radio:
      canvas_.fillRoundRect(cx - 6, cy - 3, 12, 8, 2, kGlyphColor);
      canvas_.fillCircle(cx - 3, cy + 1, 2, bgColor);  // tuning dial "cutout"
      canvas_.drawLine(cx + 4, cy - 3, cx + 7, cy - 7, kGlyphColor);  // antenna
      break;
    case IconGlyph::Wifi:
      canvas_.fillCircle(cx, cy + 6, 1, kGlyphColor);
      canvas_.drawCircleHelper(cx, cy + 6, 4, 0x03, kGlyphColor);
      canvas_.drawCircleHelper(cx, cy + 6, 7, 0x03, kGlyphColor);
      break;
    case IconGlyph::Calendar:
      canvas_.drawRoundRect(cx - 6, cy - 5, 12, 11, 1, kGlyphColor);
      canvas_.drawFastHLine(cx - 6, cy - 2, 12, kGlyphColor);
      canvas_.fillRect(cx - 4, cy - 7, 2, 3, kGlyphColor);
      canvas_.fillRect(cx + 2, cy - 7, 2, 3, kGlyphColor);
      break;
    case IconGlyph::Globe:
      canvas_.drawCircle(cx, cy, 6, kGlyphColor);
      canvas_.drawFastHLine(cx - 6, cy, 12, kGlyphColor);
      canvas_.drawFastVLine(cx, cy - 6, 12, kGlyphColor);
      break;
  }
}

void MenuSystem::renderHome(const DateTime &now) {
  // Only a live Ringing takes over the whole screen -- Snoozed falls through
  // to the normal lock-screen layout below (clock/date/status icons) with
  // just the alarm status icon turning orange, so the snooze period doesn't
  // block seeing the actual time. Was both states full-screen before this
  // got reported as too intrusive to sit through for a whole snooze
  // interval. tap:snooze/hold:dismiss still work identically either way --
  // see the Home case in handleInput(), which keys off alarms_.state()
  // directly rather than what's currently drawn.
  if (alarms_.state() == AlarmState::Ringing) {
    canvas_.setTextColor(ST77XX_RED);
    printHeader(30, 0, "ALARM");

    canvas_.setTextSize(2);
    canvas_.setTextColor(ST77XX_WHITE);
    if (alarms_.ringingAlarmIndex() >= 0) {
      const Alarm &a = alarms_.alarm(alarms_.ringingAlarmIndex());
      canvas_.setCursor(0, 26);
      canvas_.printf("%02d:%02d", a.hour, a.minute);
    }

    canvas_.setTextColor(ST77XX_ORANGE);
    canvas_.setCursor(0, 54);
    canvas_.println("Ringing");

    printHint(0, 104, "tap:snooze hold:dismiss");
    return;
  }

  // iOS-lock-screen-style layout: status icons top-right (no carrier/signal
  // area -- nothing on this device maps to that), centered date, big clock,
  // and a bottom row of app-icon-style badges instead of plain nav text.
  // Traded away versus the old layout: the inline "No radio"/sleep-timer/
  // battery-percent text rows are gone -- battery now only shows via the
  // status-bar icon, and there's no at-a-glance radio/sleep-timer detail
  // anymore (only on their own screens); "Alarms set" got its own status-bar
  // icon back (below) after that gap was reported in practice.
  bool anyAlarmEnabled = false;
  for (uint8_t i = 0; i < AlarmClock::count(); i++) {
    if (alarms_.alarm(i).enabled) {
      anyAlarmEnabled = true;
      break;
    }
  }
  drawAlarmStatusIcon(148, 3, anyAlarmEnabled, alarms_.state() == AlarmState::Snoozed);
  drawWifiStatusIcon(170, 3, wifiOnline_);
  drawBatteryStatusIcon(210, 3);

  // Top-left corner, same row as the status-bar icons on the right --
  // was centered lower down, but that left the top-left corner empty and
  // put the date awkwardly close to the big clock below it.
  canvas_.setTextSize(1);
  canvas_.setFont(&FreeSansBold9pt7b);
  canvas_.setTextColor(ST77XX_WHITE);
  char dateBuf[12];
  snprintf(dateBuf, sizeof(dateBuf), "%s %s %u", dayOfWeekName(now.dayOfTheWeek()), monthName(now.month()),
           now.day());
  setCursorTop(2, 3);
  canvas_.print(dateBuf);
  canvas_.setFont(nullptr);

  // Big clock: a real proportional font at 24pt reads far lighter/cleaner
  // than the classic bitmap font scaled up ever could -- Adafruit_GFX's
  // bundled Free Fonts don't include a true "light" weight, so plain
  // (non-bold) is the closest available approximation of a lock screen's
  // thin numerals. 12-hour mode drops the leading zero (matches "2:45" on
  // a real lock screen); 24-hour mode keeps it, since two digits is the
  // normal convention there regardless.
  uint8_t displayHour = now.hour();
  bool pm = displayHour >= 12;
  if (!timeFormat_.is24Hour()) {
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }
  char clockBuf[6];
  if (timeFormat_.is24Hour()) {
    snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", displayHour, now.minute());
  } else {
    snprintf(clockBuf, sizeof(clockBuf), "%d:%02d", displayHour, now.minute());
  }

  canvas_.setFont(&FreeSans24pt7b);
  int16_t cx1, cy1;
  uint16_t clockW, clockH;
  canvas_.getTextBounds(clockBuf, 0, 0, &cx1, &cy1, &clockW, &clockH);

  uint16_t ampmW = 0, ampmH = 0;
  if (!timeFormat_.is24Hour()) {
    canvas_.setFont(&FreeSans9pt7b);
    int16_t ax1, ay1;
    uint16_t aw;
    canvas_.getTextBounds(pm ? "PM" : "AM", 0, 0, &ax1, &ay1, &aw, &ampmH);
    ampmW = aw + 4;
  }

  int16_t startX = (kMenuScreenWidth - (int16_t)(clockW + ampmW)) / 2;
  constexpr int16_t kClockYTop = 36;

  canvas_.setFont(&FreeSans24pt7b);
  canvas_.setTextColor(ST77XX_WHITE);
  setCursorTop(startX, kClockYTop);
  canvas_.print(clockBuf);

  if (!timeFormat_.is24Hour()) {
    canvas_.setFont(&FreeSans9pt7b);
    canvas_.setTextColor(kDimGray);
    // Baseline-aligned near the bottom of the big digits, like a real lock
    // screen's small AM/PM marker.
    setCursorTop(startX + (int16_t)clockW + 4, kClockYTop + (int16_t)clockH - (int16_t)ampmH);
    canvas_.print(pm ? "PM" : "AM");
  }
  canvas_.setFont(nullptr);

  // Bottom nav: five 48px-wide slots spanning the full width, an app-icon
  // badge centered in each with its label underneath.
  struct NavItem {
    IconGlyph glyph;
    uint16_t bg;
    const char *label;
  };
  static const NavItem items[kHomeMenuItems] = {
      {IconGlyph::Bell, kIconOrange, "Alarm"},
      {IconGlyph::Radio, kIconPurple, "Radio"},
      {IconGlyph::Wifi, kIconBlue, "WiFi"},
      {IconGlyph::Calendar, kIconRed, "Date"},
      {IconGlyph::Globe, kIconGreen, "TZ"},
  };
  constexpr int16_t kSlotW = kMenuScreenWidth / kHomeMenuItems;  // 48
  constexpr int16_t kIconY = 96;
  constexpr int16_t kLabelY = 118;
  canvas_.setTextSize(1);
  for (uint8_t i = 0; i < kHomeMenuItems; i++) {
    bool focused = i == cursor_;
    int16_t slotCenter = i * kSlotW + kSlotW / 2;
    drawAppIcon(slotCenter - 10, kIconY, items[i].glyph, items[i].bg, focused);

    canvas_.setFont(focused ? &FreeSansBold9pt7b : &FreeSans9pt7b);
    canvas_.setTextColor(focused ? ST77XX_WHITE : kDimGray);
    int16_t lx1, ly1;
    uint16_t lw, lh;
    canvas_.getTextBounds(items[i].label, 0, 0, &lx1, &ly1, &lw, &lh);
    setCursorTop(slotCenter - (int16_t)lw / 2, kLabelY);
    canvas_.print(items[i].label);
  }
  canvas_.setFont(nullptr);
}

void MenuSystem::renderAlarmList() {
  printHeader(0, 0, "Alarms");
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Bell, kIconOrange, false);

  canvas_.setTextSize(2);
  canvas_.setCursor(0, 22);
  for (uint8_t i = 0; i < AlarmClock::count(); i++) {
    const Alarm &a = alarms_.alarm(i);
    canvas_.setTextColor(i == cursor_ ? ST77XX_WHITE : kDimGray);
    char timeBuf[7];
    if (timeFormat_.is24Hour()) {
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", a.hour, a.minute);
    } else {
      uint8_t h12 = a.hour % 12;
      if (h12 == 0) h12 = 12;
      snprintf(timeBuf, sizeof(timeBuf), "%d:%02d%c", h12, a.minute, a.hour >= 12 ? 'P' : 'A');
    }
    canvas_.printf("%-6s %-3s %s\n", timeBuf, a.enabled ? "ON" : "off", daysLabel(a.daysMask));
  }

  printHint(0, 118, "tap:edit hold:back");
}

void MenuSystem::renderAlarmEdit() {
  char header[16];
  snprintf(header, sizeof(header), "Alarm %u", cursor_ + 1);
  printHeader(0, 0, header);
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Bell, kIconOrange, false);
  canvas_.setCursor(0, 22);

  static const char *rows[] = {"Enabled", "Hour", "Minute", "Days", "Wake", "Save"};
  for (uint8_t i = 0; i < 6; i++) {
    // The field currently being edited is drawn bigger and white; the rest
    // stay compact and dim, so all 6 fields plus header/footer fit without
    // cramming, and it's obvious at a glance which one up/down will change.
    bool focused = i == editField_;
    canvas_.setTextSize(focused ? 2 : 1);
    canvas_.setTextColor(focused ? ST77XX_WHITE : kDimGray);
    canvas_.print(focused ? "> " : "  ");
    canvas_.print(rows[i]);
    switch (i) {
      case 0:
        canvas_.println(editingAlarm_.enabled ? ": On" : ": Off");
        break;
      case 1:
        if (timeFormat_.is24Hour()) {
          canvas_.printf(": %02d\n", editingAlarm_.hour);
        } else {
          uint8_t h12 = editingAlarm_.hour % 12;
          if (h12 == 0) h12 = 12;
          canvas_.printf(": %d %s\n", h12, editingAlarm_.hour >= 12 ? "PM" : "AM");
        }
        break;
      case 2:
        canvas_.printf(": %02d\n", editingAlarm_.minute);
        break;
      case 3:
        canvas_.print(": ");
        canvas_.println(daysLabel(editingAlarm_.daysMask));
        break;
      case 4:
        canvas_.print(": ");
        canvas_.println(wakeSourceLabel(editingAlarm_.wakeSource));
        break;
      default:
        canvas_.println();
    }
  }

  printHint(0, 122, "tap:next hold:cancel");
}

void MenuSystem::renderRadio() {
  printHeader(0, 0, "Radio");
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Radio, kIconPurple, false);

  if (!radio_.available()) {
    canvas_.setTextSize(2);
    canvas_.setTextColor(ST77XX_WHITE);
    printBold(0, 26, "No radio module");
    printBold(0, 46, "detected on I2C.");
    printHint(0, 118, "hold:back");
    return;
  }

  // On-air/muted status, as a small icon rather than a text line -- frees
  // a line below for RDS. Same Radio glyph and size as the header nav icon
  // (just left of it, same kHeaderIconY), just green/red instead of the
  // header's fixed purple.
  drawAppIcon(190, kHeaderIconY, IconGlyph::Radio, radio_.muted() ? kIconRed : kIconGreen, false);

  canvas_.setTextSize(2);
  canvas_.setTextColor(ST77XX_WHITE);
  char freqBuf[10];
  snprintf(freqBuf, sizeof(freqBuf), "%.1f MHz", radio_.frequencyMHz());
  printBold(0, 22, freqBuf);

  int16_t y = 46;
  if (seeking_) {
    // Immediate feedback that the long press registered -- seekUp()/
    // seekDown() below block for up to a couple of seconds with nothing
    // else updating the display in the meantime, so without this the
    // screen would look frozen/unresponsive during that wait.
    canvas_.setTextColor(ST77XX_ORANGE);
    printBold(0, y, "Seeking...");
  } else {
    char sigBuf[20];
    // SNR alongside RSSI (not just "Sig") since the SI4735's seek hardware
    // gates on both together -- seeing real numbers here is how
    // SeekRssiThreshold/SeekSnrThreshold in Config.h should actually get
    // tuned, rather than guessed again.
    snprintf(sigBuf, sizeof(sigBuf), "Sig %u SNR %u", radio_.rssi(), radio_.snr());
    printBold(0, y, sigBuf);
  }
  y += 16;

  char volBuf[10];
  snprintf(volBuf, sizeof(volBuf), "Vol %u", radio_.volume());
  printBold(0, y, volBuf);
  y += 16;

  // Station name (PS), harvested passively by RadioTuner::pollRdsText() --
  // see RadioTuner.h. RadioText is dashboard-only (screen space here is too
  // tight for its up to 64 chars without scrolling). Falls back to a dim
  // placeholder until something's actually been decoded (may take a few
  // seconds -- the PS name needs all 4 RDS segments to arrive). RDS Clock
  // Time sync remains a separate, still-disabled feature (see
  // RadioTuner::updateRdsSync()) and is untouched by this.
  if (radio_.stationName()[0] != '\0') {
    canvas_.setTextColor(ST77XX_WHITE);
    printBold(0, y, radio_.stationName());
  } else {
    canvas_.setTextColor(kDimGray);
    printBold(0, y, "RDS: unavailable");
  }
  y += 16;

  if (radio_.sleepTimerActive()) {
    canvas_.setTextColor(ST77XX_ORANGE);
    char sleepBuf[16];
    snprintf(sleepBuf, sizeof(sleepBuf), "Sleep %um", radio_.sleepTimerRemainingMinutes());
    printBold(0, y, sleepBuf);
  }

  printHint(0, 118, "tap:mute hold:back");
}

void MenuSystem::renderWifiInfo(const String &wifiStatusLine) {
  printHeader(0, 0, "WiFi");
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Wifi, kIconBlue, false);

  // statusLine() is already broken into a few short lines (SSID, IP,
  // hostname, and in AP mode the dashboard login) on '\n' -- splitting
  // them out here, instead of leaning on Print's own newline handling,
  // lets each be drawn at size 2 instead of the size 1 this used to be. Not
  // using printBold's double-draw here (unlike the other screens) -- on a
  // glyph that's mostly a single stroke, like the "1" an IP address often
  // starts with, the 1px offset reads as a stray mark rather than a
  // heavier weight. A pathological 32-char SSID can still run past the
  // right edge (Adafruit_GFX just clips it, no crash), but real-world
  // SSIDs/IPs/hostnames/credentials are short enough to fit.
  canvas_.setTextSize(2);
  canvas_.setTextColor(ST77XX_WHITE);
  int16_t y = 22;
  size_t start = 0;
  while (start <= wifiStatusLine.length()) {
    int nl = wifiStatusLine.indexOf('\n', start);
    String line = nl == -1 ? wifiStatusLine.substring(start) : wifiStatusLine.substring(start, nl);
    canvas_.setCursor(0, y);
    canvas_.print(line.c_str());
    y += 20;
    if (nl == -1) break;
    start = static_cast<size_t>(nl) + 1;
  }

  printHint(0, 118, "hold:back");
}

void MenuSystem::renderSetTime() {
  printHeader(0, 0, "Date & Time");
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Calendar, kIconRed, false);
  canvas_.setCursor(0, 24);

  // Day-of-week isn't a field here -- it's derived from Year/Month/Day
  // (RTClib's DateTime::dayOfTheWeek()), so getting the date right is what
  // keeps it correct; there's nothing separate to set.
  static const char *rows[] = {"Year", "Month", "Day", "Hour", "Minute"};
  for (uint8_t i = 0; i < 5; i++) {
    // The field currently being edited is drawn bigger and white; the rest
    // stay compact and dim, so all 8 rows plus header/footer fit without
    // cramming, and it's obvious at a glance which one up/down will change.
    bool focused = i == editField_;
    canvas_.setTextSize(focused ? 2 : 1);
    canvas_.setTextColor(focused ? ST77XX_WHITE : kDimGray);
    canvas_.print(focused ? "> " : "  ");
    canvas_.print(rows[i]);
    switch (i) {
      case 0:
        canvas_.printf(": %u\n", editingYear_);
        break;
      case 1:
        canvas_.print(": ");
        canvas_.println(monthName(editingMonth_));
        break;
      case 2:
        canvas_.printf(": %02d\n", editingDay_);
        break;
      case 3:
        canvas_.printf(": %02d\n", editingHour_);
        break;
      case 4:
        canvas_.printf(": %02d\n", editingMinute_);
        break;
    }
  }

  // Format row: up/down toggles 24h/12h, applied (and persisted) right
  // away -- same as the Timezone screen's immediate-apply cycling.
  {
    bool focused = editField_ == 5;
    canvas_.setTextSize(focused ? 2 : 1);
    canvas_.setTextColor(focused ? ST77XX_WHITE : kDimGray);
    canvas_.print(focused ? "> " : "  ");
    canvas_.print("Format: ");
    canvas_.println(timeFormat_.is24Hour() ? "24h" : "12h");
  }

  // Sync Now: fires on up/down, not tap (see the handleInput comment) --
  // greyed out and unresponsive when there's no uplink to sync against,
  // the same visual language dim rows already use elsewhere for "can't
  // interact with this right now" (e.g. Radio's entries when unavailable).
  {
    bool focused = editField_ == 6;
    bool enabled = wifiOnline_;
    canvas_.setTextSize(focused ? 2 : 1);
    canvas_.setTextColor(!enabled ? kDimGray : (focused ? ST77XX_WHITE : kDimGray));
    canvas_.print(focused ? "> " : "  ");
    canvas_.println("Sync Now");
  }

  // Save: the only row that fires on tap.
  {
    bool focused = editField_ == 7;
    canvas_.setTextSize(focused ? 2 : 1);
    canvas_.setTextColor(focused ? ST77XX_WHITE : kDimGray);
    canvas_.print(focused ? "> " : "  ");
    canvas_.println("Save");
  }

  if (!rtc_ || !rtcAvailable_) {
    canvas_.setTextSize(1);
    canvas_.setTextColor(ST77XX_RED);
    canvas_.setCursor(0, 102);
    canvas_.println("(no RTC -- won't save)");
  }
  printHint(0, 118, "tap:next hold:cancel");
}

void MenuSystem::renderTimezone() {
  printHeader(0, 0, "Timezone");
  drawAppIcon(kHeaderIconX, kHeaderIconY, IconGlyph::Globe, kIconGreen, false);

  // The full label ("Central Europe (Paris/Berlin)") can run to 30
  // characters -- too wide for a legible size on one line -- but every
  // entry follows "Region (City)", so splitting at the parenthetical keeps
  // both halves comfortably under the screen width even at size 2.
  canvas_.setTextSize(2);
  canvas_.setTextColor(ST77XX_WHITE);
  const char *label = timezone_.label();
  const char *paren = strchr(label, '(');
  if (paren) {
    char region[24];
    size_t regionLen = static_cast<size_t>(paren - label);
    if (regionLen > 0 && label[regionLen - 1] == ' ') regionLen--;  // trim trailing space
    if (regionLen >= sizeof(region)) regionLen = sizeof(region) - 1;
    memcpy(region, label, regionLen);
    region[regionLen] = '\0';
    printBold(0, 22, region);
    printBold(0, 42, paren);
  } else {
    printBold(0, 22, label);  // e.g. "UTC" -- no city to split off
  }

  canvas_.setTextSize(1);
  canvas_.setFont(&FreeSans9pt7b);
  setCursorTop(0, 70);
  canvas_.setTextColor(kDimGray);
  canvas_.println("Takes effect on the");
  canvas_.println("next NTP sync.");
  canvas_.setFont(nullptr);

  printHint(0, 118, "up/down:change hold:back");
}

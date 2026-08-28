#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RTClib.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_LEDBackpack.h>

#include "AlarmClock.h"
#include "AlarmSound.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DebouncedButton.h"
#include "DisplayDimmer.h"
#include "MenuSystem.h"
#include "RadioTuner.h"
#include "SnoozeController.h"
#include "StateLock.h"
#include "TimeFormatStore.h"
#include "TimezoneStore.h"
#include "WakeController.h"
#include "WebDashboard.h"

// Reverse TFT Feather cuts power to the STEMMA QT/I2C bus by default;
// TFT_I2C_POWER must be driven HIGH before any I2C peripheral will respond.
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

RTC_DS3231 rtc;
Adafruit_VEML7700 lightSensor;
Adafruit_7segment sevenSegment = Adafruit_7segment();
BatteryMonitor battery;
TimezoneStore timezoneStore;
TimeFormatStore timeFormat;

AlarmClock alarmClock;
RadioTuner radioTuner;
AlarmSound alarmSound;
WakeController wakeController(alarmClock, radioTuner, alarmSound);
SnoozeController snoozeController(alarmClock, radioTuner);
MenuSystem menu(tft, alarmClock, radioTuner, &battery, &rtc, timezoneStore, timeFormat);
WebDashboard dashboard(alarmClock, radioTuner, &rtc, &battery, timezoneStore);

DebouncedButton volumeUpButton(Pins::VolumeUp);
DebouncedButton volumeDownButton(Pins::VolumeDown);
DebouncedButton snoozeButton(Pins::SnoozeButton);

static bool rtcOk = false;
static bool lightSensorOk = false;
static bool sevenSegmentOk = false;

// Cached once a second from the RTC so the menu's per-loop button polling
// doesn't hit the I2C bus on every iteration. Defaults to the RTClib epoch
// (2000-01-01 00:00:00) until the first real read, i.e. before the RTC is
// wired up the clock will just show that placeholder.
static DateTime cachedNow;

// Dim gray for the subtitle -- not one of Adafruit_ST77xx's named colors, so
// computed directly (RGB565, ~mid-gray).
constexpr uint16_t kDimGray = 0x8410;

// y positions for each bring-up line, spaced enough to breathe at text size
// 1 (8px glyph height): the two-line, size-2 bold title/subtitle block, then
// 6 status lines 14px apart -- ends around y=114, comfortably inside the
// 240x135 landscape canvas.
constexpr int16_t kSubtitleY = 20;
constexpr int16_t kStatusStartY = 36;
constexpr int16_t kStatusLineHeight = 14;

static uint8_t statusLineIndex = 0;

// Faked bold: draws the classic bitmap font twice, offset by one pixel, so
// strokes overlap and thicken. Adafruit_GFX's built-in font has no bold
// weight of its own.
static void printBold(int16_t x, int16_t y, const char *text) {
  tft.setCursor(x + 1, y);
  tft.print(text);
  tft.setCursor(x, y);
  tft.print(text);
}

static void reportStatus(const char *label, bool ok) {
  Serial.printf("%-8s %s\n", label, ok ? "OK" : "FAILED");

  int16_t y = kStatusStartY + statusLineIndex * kStatusLineHeight;
  statusLineIndex++;

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, y);
  tft.print(label);
  tft.setTextColor(ok ? ST77XX_GREEN : ST77XX_RED);
  tft.setCursor(72, y);
  tft.print(ok ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);

  // esp_reset_reason() reads a value the chip stores in RTC memory across
  // resets, so it's reliable even when a crash happens too abruptly for
  // anything to reach the serial port beforehand (a brownout in particular
  // never prints anything on its own) -- cheap enough to just always log.
  const char *resetReason = "UNKNOWN";
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      resetReason = "POWERON (normal power-up)";
      break;
    case ESP_RST_EXT:
      resetReason = "EXT (reset pin/button)";
      break;
    case ESP_RST_SW:
      resetReason = "SW (esp_restart() called)";
      break;
    case ESP_RST_PANIC:
      resetReason = "PANIC (crash)";
      break;
    case ESP_RST_INT_WDT:
      resetReason = "INT_WDT (interrupt watchdog)";
      break;
    case ESP_RST_TASK_WDT:
      resetReason = "TASK_WDT (task watchdog)";
      break;
    case ESP_RST_WDT:
      resetReason = "WDT (other watchdog)";
      break;
    case ESP_RST_BROWNOUT:
      resetReason = "BROWNOUT (power dip)";
      break;
    case ESP_RST_DEEPSLEEP:
      resetReason = "DEEPSLEEP wake";
      break;
    default:
      break;
  }
  Serial.printf("Reset reason: %s\n", resetReason);

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);

  // Backlight is PWM-driven so the auto-dim loop below can vary it; start
  // at full brightness for the bring-up status screen.
  pinMode(TFT_BACKLITE, OUTPUT);
  analogWrite(TFT_BACKLITE, DisplayConfig::MaxTftBacklight);

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_ORANGE);
  printBold(0, 0, "ESP32 Alarm Clock");

  tft.setTextSize(1);
  tft.setTextColor(kDimGray);
  tft.setCursor(0, kSubtitleY);
  tft.print("Hardware bring-up");

  Wire.begin();

  rtcOk = rtc.begin();
  reportStatus("RTC", rtcOk);
  if (rtcOk) {
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    cachedNow = rtc.now();
  } else {
    // menu/dashboard are constructed (and handed &rtc) before rtc.begin() is
    // ever called -- global objects are initialized before setup() runs, so
    // there was no way to know yet whether the hardware actually responded.
    // Tell them now, so their own "no RTC" fallbacks (Set Time's warning,
    // skipping NTP-driven adjust() calls, the dashboard's status "time"
    // field) actually engage instead of trusting a non-functional RTC.
    menu.setRtcAvailable(false);
    dashboard.setRtcAvailable(false);
  }

  lightSensorOk = lightSensor.begin();
  reportStatus("Light", lightSensorOk);

  sevenSegmentOk = sevenSegment.begin(0x70);
  reportStatus("7-seg", sevenSegmentOk);
  if (sevenSegmentOk) {
    sevenSegment.clear();
    sevenSegment.writeDisplay();
  }

  battery.begin();
  reportStatus("Battery", battery.available());
  reportStatus("Buzzer", alarmSound.begin());
  timezoneStore.begin();
  timeFormat.begin();

  pinMode(Pins::VolumeUp, INPUT_PULLUP);
  pinMode(Pins::VolumeDown, INPUT_PULLUP);
  pinMode(Pins::SnoozeButton, INPUT_PULLUP);

  alarmClock.begin();
  reportStatus("Radio", radioTuner.begin());
  menu.begin();
  dashboard.begin();  // may take a few seconds: WiFi connect attempt + NTP sync

  delay(8000);  // leave the bring-up status readable before the menu takes over
}

void loop() {
  // Held for the whole iteration -- see StateLock.h. Keeps this entire
  // function mutually exclusive with every WebDashboard route handler,
  // which runs on AsyncTCP's own task, not this one.
  StateLock lock;

  // Fast path: keeps menu button response, the web server, and any playing
  // alarm tone snappy.
  dashboard.update();
  menu.update(cachedNow, dashboard.statusLine(), dashboard.isOnline());
  // "Sync Now" on the Date & Time screen: blocks for up to 5s (same as the
  // automatic daily resync already does from within this same locked
  // loop() body), so skip it outright if we're not even online rather than
  // block pointlessly. MenuSystem also greys the row out and won't raise
  // the request at all while offline, but isOnline() can still have
  // flipped between that check and this one.
  if (menu.consumeNtpSyncRequest() && dashboard.isOnline()) {
    dashboard.syncTimeFromNtp();
  }
  wakeController.tickFast();

  volumeUpButton.update();
  volumeDownButton.update();
  if (volumeUpButton.justPressed()) radioTuner.volumeUp();
  if (volumeDownButton.justPressed()) radioTuner.volumeDown();

  snoozeButton.update();
  if (snoozeButton.justPressed()) snoozeController.onSnoozePressed(cachedNow);

  static uint32_t lastTickMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastTickMs < 1000) {
    return;
  }
  lastTickMs = nowMs;

  radioTuner.update();  // expires the sleep timer

  if (rtcOk) {
    cachedNow = rtc.now();
    alarmClock.update(cachedNow);
    wakeController.tickSlow(cachedNow);
    Serial.printf("%02d:%02d:%02d\n", cachedNow.hour(), cachedNow.minute(), cachedNow.second());

    if (sevenSegmentOk && alarmClock.state() == AlarmState::Idle) {
      if (timeFormat.is24Hour()) {
        sevenSegment.print(cachedNow.hour() * 100 + cachedNow.minute(), DEC);
      } else {
        // No letters on a 4-digit 7-segment, so AM/PM rides on the last
        // digit's decimal point instead (lit = PM) -- a common convention
        // on this style of display. Leading hour digit is blanked rather
        // than shown as 0 (e.g. "9:05", not "09:05").
        uint8_t displayHour = cachedNow.hour() % 12;
        if (displayHour == 0) displayHour = 12;
        bool pm = cachedNow.hour() >= 12;
        if (displayHour >= 10) {
          sevenSegment.writeDigitNum(0, displayHour / 10);
        } else {
          sevenSegment.writeDigitRaw(0, 0x00);
        }
        sevenSegment.writeDigitNum(1, displayHour % 10);
        sevenSegment.writeDigitNum(3, cachedNow.minute() / 10);
        sevenSegment.writeDigitNum(4, cachedNow.minute() % 10, pm);
      }
      sevenSegment.drawColon(cachedNow.second() % 2 == 0);
      sevenSegment.writeDisplay();
    }
  }

  if (lightSensorOk) {
    float lux = lightSensor.readLux();
    Serial.printf("Ambient lux: %.1f\n", lux);
    analogWrite(TFT_BACKLITE, DisplayDimmer::tftBacklightFor(lux));
    if (sevenSegmentOk) sevenSegment.setBrightness(DisplayDimmer::sevenSegmentBrightnessFor(lux));
  }
}

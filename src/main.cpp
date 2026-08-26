#include <Arduino.h>
#include <Wire.h>
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

AlarmClock alarmClock;
RadioTuner radioTuner;
AlarmSound alarmSound;
WakeController wakeController(alarmClock, radioTuner, alarmSound);
SnoozeController snoozeController(alarmClock, radioTuner);
MenuSystem menu(tft, alarmClock, radioTuner, &battery, &rtc, timezoneStore);
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

static void reportStatus(const char *label, bool ok) {
  Serial.printf("%-16s %s\n", label, ok ? "OK" : "FAILED");
  tft.printf("%-14s %s\n", label, ok ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);

  // Backlight is PWM-driven so the auto-dim loop below can vary it; start
  // at full brightness for the bring-up status screen.
  pinMode(TFT_BACKLITE, OUTPUT);
  analogWrite(TFT_BACKLITE, DisplayConfig::MaxTftBacklight);

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("ESP32 radio alarm clock");
  tft.println("Hardware bring-up");
  tft.println();

  Wire.begin();

  rtcOk = rtc.begin();
  reportStatus("RTC (DS3231)", rtcOk);
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
  reportStatus("Light (VEML7700)", lightSensorOk);

  sevenSegmentOk = sevenSegment.begin(0x70);
  reportStatus("7-segment display", sevenSegmentOk);
  if (sevenSegmentOk) {
    sevenSegment.clear();
    sevenSegment.writeDisplay();
  }

  reportStatus("Battery (MAX17048)", battery.begin());
  reportStatus("Alarm tone (buzzer)", alarmSound.begin());
  timezoneStore.begin();

  pinMode(Pins::VolumeUp, INPUT_PULLUP);
  pinMode(Pins::VolumeDown, INPUT_PULLUP);
  pinMode(Pins::SnoozeButton, INPUT_PULLUP);

  alarmClock.begin();
  radioTuner.begin();
  menu.begin();
  dashboard.begin();  // may take a few seconds: WiFi connect attempt + NTP sync

  delay(1000);  // leave the bring-up status readable before the menu takes over
}

void loop() {
  // Fast path: keeps menu button response, the web server, and any playing
  // alarm tone snappy.
  dashboard.update();
  menu.update(cachedNow, dashboard.statusLine());
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
      sevenSegment.print(cachedNow.hour() * 100 + cachedNow.minute(), DEC);
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

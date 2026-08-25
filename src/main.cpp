#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RTClib.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_LEDBackpack.h>

// Reverse TFT Feather cuts power to the STEMMA QT/I2C bus by default;
// TFT_I2C_POWER must be driven HIGH before any I2C peripheral will respond.
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

RTC_DS3231 rtc;
Adafruit_VEML7700 lightSensor;
Adafruit_7segment sevenSegment = Adafruit_7segment();

// Onboard buttons (Adafruit ESP32-S3 Reverse TFT Feather pinout).
// D0 shares the boot-strap pin; safe to read as input once past boot.
static constexpr uint8_t BUTTON_D0 = 0;
static constexpr uint8_t BUTTON_D1 = 1;
static constexpr uint8_t BUTTON_D2 = 2;

static bool rtcOk = false;
static bool lightSensorOk = false;
static bool sevenSegmentOk = false;

static void reportStatus(const char *label, bool ok) {
  Serial.printf("%-16s %s\n", label, ok ? "OK" : "FAILED");
  tft.printf("%-14s %s\n", label, ok ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);

  pinMode(BUTTON_D0, INPUT_PULLUP);
  pinMode(BUTTON_D1, INPUT_PULLUP);
  pinMode(BUTTON_D2, INPUT_PULLUP);

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
  if (rtcOk && rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  lightSensorOk = lightSensor.begin();
  reportStatus("Light (VEML7700)", lightSensorOk);

  sevenSegmentOk = sevenSegment.begin(0x70);
  reportStatus("7-segment display", sevenSegmentOk);
  if (sevenSegmentOk) {
    sevenSegment.clear();
    sevenSegment.writeDisplay();
  }
}

void loop() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 1000) {
    return;
  }
  lastUpdate = now;

  if (rtcOk) {
    DateTime t = rtc.now();
    Serial.printf("%02d:%02d:%02d\n", t.hour(), t.minute(), t.second());

    if (sevenSegmentOk) {
      sevenSegment.print(t.hour() * 100 + t.minute(), DEC);
      sevenSegment.drawColon(t.second() % 2 == 0);
      sevenSegment.writeDisplay();
    }
  }

  if (lightSensorOk) {
    Serial.printf("Ambient lux: %.1f\n", lightSensor.readLux());
  }
}

#pragma once

#include <ESPAsyncWebServer.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "Config.h"
#include "RadioTuner.h"

// Hosts the setup/status web dashboard described in the README's "Planned
// Features": AP-mode WiFi provisioning on first boot, then a normal
// dashboard reachable at NetConfig::MdnsHostname + ".local" once the device
// has joined the home network. rtc may be null (no time-of-day features
// until the RTC is wired up); the dashboard degrades gracefully without it.
class WebDashboard {
 public:
  WebDashboard(AlarmClock &alarms, RadioTuner &radio, RTC_DS3231 *rtc);

  void begin();
  // Call every loop iteration; only does real work when a WiFi credential
  // change has queued a restart (the server itself runs on its own task).
  void update();

  // Short human-readable summary for the on-device WiFi info screen.
  String statusLine() const;

 private:
  bool connectStation(const String &ssid, const String &password);
  void startAccessPoint();
  void registerRoutes();
  String buildStatusJson();
  String buildAlarmsJson();
  String buildRadioJson();
  static void loadWifiCredentials(String &ssid, String &password);
  static void saveWifiCredentials(const String &ssid, const String &password);

  AsyncWebServer server_{80};
  AlarmClock &alarms_;
  RadioTuner &radio_;
  RTC_DS3231 *rtc_;

  bool apMode_ = true;
  String staSsid_;
  uint32_t restartAtMs_ = 0;  // 0 = no restart pending
};

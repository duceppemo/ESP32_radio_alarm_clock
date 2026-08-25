#pragma once

#include <ESPAsyncWebServer.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "RadioTuner.h"
#include "TimezoneStore.h"

// Hosts the setup/status web dashboard described in the README's "Planned
// Features": AP-mode WiFi provisioning on first boot, then a normal
// dashboard reachable at NetConfig::MdnsHostname + ".local" once the device
// has joined the home network. rtc/battery may be null (features degrade
// gracefully) until that hardware is wired up.
//
// Once on the home network it also syncs the RTC from NTP (re-synced daily,
// using timezone's currently-selected POSIX TZ string for the UTC->local
// conversion) and exposes an OTA firmware-update page at /update via
// ElegantOTA.
class WebDashboard {
 public:
  WebDashboard(AlarmClock &alarms, RadioTuner &radio, RTC_DS3231 *rtc, BatteryMonitor *battery,
               TimezoneStore &timezone);

  void begin();
  // Call every loop iteration: services a queued restart after a WiFi
  // credential change, and re-syncs NTP once a day (the server itself runs
  // on its own task and doesn't need ticking).
  void update();

  // Short human-readable summary for the on-device WiFi info screen.
  String statusLine() const;

 private:
  bool connectStation(const String &ssid, const String &password);
  void startAccessPoint();
  void registerRoutes();
  void syncTimeFromNtp();
  String buildStatusJson();
  String buildAlarmsJson();
  String buildRadioJson();
  String buildSettingsJson();
  String buildTimezoneJson();
  bool applySettingsJson(JsonVariantConst doc);
  static void loadWifiCredentials(String &ssid, String &password);
  static void saveWifiCredentials(const String &ssid, const String &password);

  AsyncWebServer server_{80};
  AlarmClock &alarms_;
  RadioTuner &radio_;
  RTC_DS3231 *rtc_;
  BatteryMonitor *battery_;
  TimezoneStore &timezone_;

  bool apMode_ = true;
  String staSsid_;
  uint32_t restartAtMs_ = 0;   // 0 = no restart pending
  uint32_t lastNtpSyncMs_ = 0; // 0 = never synced yet this boot
};

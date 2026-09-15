#pragma once

#include <ESPAsyncWebServer.h>
#include <RTClib.h>

#include "AlarmClock.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "RadioTuner.h"
#include "RegionStore.h"
#include "TimeFormatStore.h"
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
//
// Login: the setup AP itself is the trust boundary (same as any open
// provisioning AP -- you need to already be near the device to join it), so
// the dashboard requires no login while apMode_ is true, and shows the
// randomly-generated default password right on that AP-mode page so it can
// be carried over to the home network. Once on the home network (STA mode),
// every route (dashboard page, JSON API, and always /update regardless of
// mode) requires HTTP Basic Auth against the stored admin credentials.
class WebDashboard {
 public:
  WebDashboard(AlarmClock &alarms, RadioTuner &radio, RTC_DS3231 *rtc, BatteryMonitor *battery,
               TimezoneStore &timezone, RegionStore &region, TimeFormatStore &timeFormat);

  void begin();
  // rtc is constructed and wired up before rtc->begin() is ever called
  // (it's a global, initialized before setup() runs), so the constructor
  // can't know yet whether the hardware actually responded -- call this
  // once setup() finds out, same as MenuSystem::setRtcAvailable().
  void setRtcAvailable(bool available) { rtcAvailable_ = available; }
  // Call every loop iteration: services a queued restart after a WiFi
  // credential change, and re-syncs NTP once a day (the server itself runs
  // on its own task and doesn't need ticking).
  void update();

  // Short human-readable summary for the on-device WiFi info screen.
  String statusLine() const;

  // True once joined to the home network (STA mode); false while still on
  // the open setup AP, where there's no real internet uplink to sync
  // against. For callers deciding whether syncTimeFromNtp() is worth
  // attempting -- it already tolerates being called with no network
  // (getLocalTime() just times out), so this isn't required, only a way to
  // skip a pointless multi-second block when already known to be offline.
  bool isOnline() const { return !apMode_; }

  // True once syncTimeFromNtp() has actually succeeded at least once this
  // boot -- lastNtpSyncMs_ only tracks the last *attempt* (deliberately, to
  // avoid retrying every loop tick after a failure), so this is what
  // main.cpp needs to correctly gate the RDS Clock Time fallback sync
  // ("NTP unavailable or hasn't synced yet" -- never reset back to false,
  // since a later failed daily resync shouldn't suddenly re-arm the RDS
  // fallback when the RTC is already NTP-accurate from the last one).
  bool hasSyncedFromNtpSuccessfully() const { return ntpSyncSucceededOnce_; }

  // Forces an immediate NTP resync -- normally this happens automatically
  // (once on boot, then daily), but MenuSystem's Date & Time screen offers
  // a manual "Sync Now" too. Callers must already hold a StateLock (see
  // StateLock.h): this touches rtc_/timezone_, the same objects loop() and
  // every dashboard route handler share.
  void syncTimeFromNtp();

 private:
  bool connectStation(const String &ssid, const String &password);
  void startAccessPoint();
  void registerRoutes();
  String buildStatusJson();
  String buildAlarmsJson();
  String buildRadioJson();
  String buildSettingsJson();
  String buildTimezoneJson();
  String buildSecurityJson();
  bool applySettingsJson(JsonVariantConst doc);
  static void loadWifiCredentials(String &ssid, String &password);
  static void saveWifiCredentials(const String &ssid, const String &password);
  void loadOrCreateAdminCredentials();
  void saveAdminCredentials(const String &username, const String &password);
  // Returns true (and lets the caller proceed) if authenticated or if
  // apMode_ makes auth a no-op; otherwise sends a 401 challenge and returns
  // false -- callers must return immediately when this returns false.
  bool requireAuth(AsyncWebServerRequest *request) const;

  AsyncWebServer server_{80};
  AlarmClock &alarms_;
  RadioTuner &radio_;
  RTC_DS3231 *rtc_;
  bool rtcAvailable_ = true;
  BatteryMonitor *battery_;
  TimezoneStore &timezone_;
  RegionStore &region_;
  TimeFormatStore &timeFormat_;

  bool apMode_ = true;
  String staSsid_;
  String adminUsername_;
  String adminPassword_;
  uint32_t restartAtMs_ = 0;   // 0 = no restart pending
  uint32_t lastNtpSyncMs_ = 0; // 0 = never synced yet this boot
  bool ntpSyncSucceededOnce_ = false;
};

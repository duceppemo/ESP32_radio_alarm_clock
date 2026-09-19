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
// Once on the home network it also keeps the RTC in step with NTP (see
// NetConfig's NTP constants for how: SNTP maintains the ESP's own clock in
// the background, and the RTC is compared against its local-time view --
// timezone's currently-selected POSIX TZ string, DST included -- every few
// seconds and nudged only when they disagree) and exposes an OTA
// firmware-update page at /update via ElegantOTA.
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
  // credential change, retries the WiFi link (STA reconnect, or the
  // AP-mode retry of stored credentials), and keeps the RTC in step with
  // NTP. Everything in here is non-blocking -- the server itself runs on
  // its own task and doesn't need ticking.
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

  // True once the RTC has been set from NTP at least once this boot -- what
  // main.cpp needs to gate the RDS Clock Time fallback sync ("NTP
  // unavailable or hasn't synced yet"). Never reset back to false: a later
  // WiFi outage shouldn't re-arm the RDS fallback when the RTC is already
  // NTP-accurate from before it.
  bool hasSyncedFromNtpSuccessfully() const { return ntpSyncSucceededOnce_; }

  // Asks for the RTC to be re-checked against NTP on the next update()
  // (also re-applies the timezone, so a TZ change takes effect right away
  // instead of at the next periodic check). Normally unnecessary -- the
  // check runs every NtpCheckIntervalMs on its own -- but MenuSystem's
  // Date & Time screen offers a manual "Sync Now", and the timezone/
  // settings routes call it after changing the TZ. Non-blocking.
  void requestNtpSync() { ntpSyncRequested_ = true; }

  // Current local time straight from the ESP's SNTP-maintained clock, for
  // callers with no working RTC (main.cpp falls back to this for the alarm
  // schedule and the 7-segment when rtc.begin() failed). False -- and out
  // untouched -- unless SNTP has heard from a server within NtpFreshnessMs.
  bool ntpLocalTime(DateTime &out) const;

 private:
  bool connectStation(const String &ssid, const String &password);
  void startAccessPoint();
  // The STA link is up (at boot, or after an AP-mode retry finally joined):
  // drop the AP, start mDNS, start SNTP.
  void becomeStation();
  // (Re)starts SNTP against NtpServer with the current timezone's TZ rule.
  void startSntp();
  // The periodic RTC-vs-NTP comparison -- see NetConfig's NTP constants.
  void reconcileRtcWithNtp();
  // The RTC if there is one, else ntpLocalTime(); false if neither.
  bool currentTime(DateTime &out) const;
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
  String staSsid_;      // stored credentials, empty if none (loaded at begin())
  String staPassword_;
  String adminUsername_;
  String adminPassword_;
  bool restartPending_ = false;
  uint32_t restartRequestedMs_ = 0;
  bool ntpSyncSucceededOnce_ = false;
  bool ntpSyncRequested_ = false;
  uint32_t lastNtpCheckMs_ = 0;
  uint32_t lastWifiReconnectMs_ = 0;
  // AP mode with stored credentials: a non-blocking WiFi.begin() is in
  // flight alongside the AP (WIFI_AP_STA); update() watches for it to land.
  bool staRetryInFlight_ = false;
  uint32_t lastStaRetryMs_ = 0;
};

#include "WebDashboard.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "DashboardPage.h"

namespace {
constexpr const char *kWifiNamespace = "wifi";
constexpr const char *kSsidKey = "ssid";
constexpr const char *kPasswordKey = "password";

const char *alarmStateName(AlarmState state) {
  switch (state) {
    case AlarmState::Ringing:
      return "ringing";
    case AlarmState::Snoozed:
      return "snoozed";
    default:
      return "idle";
  }
}
}  // namespace

WebDashboard::WebDashboard(AlarmClock &alarms, RadioTuner &radio, RTC_DS3231 *rtc)
    : alarms_(alarms), radio_(radio), rtc_(rtc) {}

void WebDashboard::begin() {
  String ssid, password;
  loadWifiCredentials(ssid, password);

  if (ssid.length() > 0 && connectStation(ssid, password)) {
    apMode_ = false;
    staSsid_ = ssid;
    if (MDNS.begin(NetConfig::MdnsHostname)) {
      MDNS.addService("http", "tcp", 80);
    }
  } else {
    startAccessPoint();
  }

  registerRoutes();
  server_.begin();
}

void WebDashboard::update() {
  if (restartAtMs_ != 0 && millis() >= restartAtMs_) {
    ESP.restart();
  }
}

String WebDashboard::statusLine() const {
  if (apMode_) {
    return String("AP: ") + NetConfig::ApSsid + "\n" + WiFi.softAPIP().toString();
  }
  return String("STA: ") + staSsid_ + "\n" + WiFi.localIP().toString() + "\n" +
         NetConfig::MdnsHostname + ".local";
}

bool WebDashboard::connectStation(const String &ssid, const String &password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < NetConfig::StaConnectTimeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void WebDashboard::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(NetConfig::ApSsid);
  apMode_ = true;
}

void WebDashboard::registerRoutes() {
  server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", kDashboardHtml);
  });

  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStatusJson());
  });

  server_.on("/api/radio", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildRadioJson());
  });

  server_.on("/api/alarms", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildAlarmsJson());
  });

  server_.on("/api/alarm/snooze", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (rtc_) alarms_.snooze(rtc_->now());
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/alarm/dismiss", HTTP_POST, [this](AsyncWebServerRequest *request) {
    alarms_.dismiss();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  auto *wifiHandler = new AsyncCallbackJsonWebHandler(
      "/api/wifi", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        String ssid = json["ssid"] | "";
        String password = json["password"] | "";
        if (ssid.isEmpty()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
          return;
        }
        saveWifiCredentials(ssid, password);
        request->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
        restartAtMs_ = millis() + 1500;
      });
  wifiHandler->setMethod(HTTP_POST);
  server_.addHandler(wifiHandler);

  auto *alarmHandler = new AsyncCallbackJsonWebHandler(
      "/api/alarms", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        int index = json["index"] | -1;
        if (index < 0 || index >= AlarmClock::count()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid index\"}");
          return;
        }
        Alarm a;
        a.enabled = json["enabled"] | false;
        a.hour = json["hour"] | 7;
        a.minute = json["minute"] | 0;
        a.daysMask = 0;
        JsonArray days = json["days"];
        for (uint8_t i = 0; i < 7 && i < days.size(); i++) {
          if (days[i].as<bool>()) a.daysMask |= (1 << i);
        }
        alarms_.setAlarm(index, a);
        request->send(200, "application/json", "{\"ok\":true}");
      });
  alarmHandler->setMethod(HTTP_POST);
  server_.addHandler(alarmHandler);

  auto *radioHandler = new AsyncCallbackJsonWebHandler(
      "/api/radio", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        String action = json["action"] | "";
        int value = json["value"] | 0;

        if (action == "tune") {
          radio_.tune((uint16_t)value);
        } else if (action == "seekUp") {
          radio_.seekUp();
        } else if (action == "seekDown") {
          radio_.seekDown();
        } else if (action == "volume") {
          radio_.setVolume((uint8_t)value);
        } else if (action == "toggleMute") {
          radio_.setMuted(!radio_.muted());
        } else if (action == "recallPreset") {
          radio_.recallPreset((uint8_t)value);
        } else if (action == "storePreset") {
          radio_.storePreset((uint8_t)value, radio_.frequency10kHz());
        } else {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown action\"}");
          return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
      });
  radioHandler->setMethod(HTTP_POST);
  server_.addHandler(radioHandler);

  server_.onNotFound(
      [](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
}

String WebDashboard::buildStatusJson() {
  JsonDocument doc;
  doc["mode"] = apMode_ ? "ap" : "sta";
  doc["ip"] = apMode_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  if (!apMode_) doc["ssid"] = staSsid_;
  if (rtc_) {
    DateTime now = rtc_->now();
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    doc["time"] = buf;
  }
  doc["alarmState"] = alarmStateName(alarms_.state());
  String out;
  serializeJson(doc, out);
  return out;
}

String WebDashboard::buildAlarmsJson() {
  JsonDocument doc;
  doc["state"] = alarmStateName(alarms_.state());
  doc["ringingIndex"] = alarms_.ringingAlarmIndex();
  doc["snoozeMinutes"] = alarms_.snoozeMinutes();
  JsonArray arr = doc["alarms"].to<JsonArray>();
  for (uint8_t i = 0; i < AlarmClock::count(); i++) {
    const Alarm &a = alarms_.alarm(i);
    JsonObject o = arr.add<JsonObject>();
    o["enabled"] = a.enabled;
    o["hour"] = a.hour;
    o["minute"] = a.minute;
    JsonArray days = o["days"].to<JsonArray>();
    for (uint8_t d = 0; d < 7; d++) days.add((bool)(a.daysMask & (1 << d)));
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String WebDashboard::buildRadioJson() {
  JsonDocument doc;
  doc["frequency10kHz"] = radio_.frequency10kHz();
  doc["volume"] = radio_.volume();
  doc["muted"] = radio_.muted();
  doc["rssi"] = radio_.rssi();
  JsonArray presets = doc["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < radio_.presetCount(); i++) presets.add(radio_.preset(i));
  String out;
  serializeJson(doc, out);
  return out;
}

void WebDashboard::loadWifiCredentials(String &ssid, String &password) {
  Preferences prefs;
  prefs.begin(kWifiNamespace, true);
  ssid = prefs.getString(kSsidKey, "");
  password = prefs.getString(kPasswordKey, "");
  prefs.end();
}

void WebDashboard::saveWifiCredentials(const String &ssid, const String &password) {
  Preferences prefs;
  prefs.begin(kWifiNamespace, false);
  prefs.putString(kSsidKey, ssid);
  prefs.putString(kPasswordKey, password);
  prefs.end();
}

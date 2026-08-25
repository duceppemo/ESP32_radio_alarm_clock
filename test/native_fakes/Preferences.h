#pragma once

// Minimal in-memory stand-in for the ESP32 core's Preferences (NVS wrapper),
// for native unit tests -- Preferences itself is ESP32-core-specific and
// isn't provided by ArduinoFake. Only implements what AlarmClock actually
// calls. Backed by one process-wide static map, so tests must call
// Preferences::resetAll() between cases to avoid leaking state.

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false) {
    ns_ = name;
    (void)readOnly;
    return true;
  }
  void end() {}

  size_t putUChar(const char *key, uint8_t value) {
    store()[fullKey(key)] = {value};
    return 1;
  }
  uint8_t getUChar(const char *key, uint8_t defaultValue = 0) {
    auto it = store().find(fullKey(key));
    if (it == store().end() || it->second.size() != 1) return defaultValue;
    return it->second[0];
  }

  size_t putUShort(const char *key, uint16_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    memcpy(bytes.data(), &value, sizeof(value));
    store()[fullKey(key)] = bytes;
    return sizeof(value);
  }
  uint16_t getUShort(const char *key, uint16_t defaultValue = 0) {
    auto it = store().find(fullKey(key));
    if (it == store().end() || it->second.size() != sizeof(defaultValue)) return defaultValue;
    uint16_t value;
    memcpy(&value, it->second.data(), sizeof(value));
    return value;
  }

  size_t putBytes(const char *key, const void *value, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(value);
    store()[fullKey(key)] = std::vector<uint8_t>(p, p + len);
    return len;
  }
  size_t getBytesLength(const char *key) {
    auto it = store().find(fullKey(key));
    return it == store().end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *key, void *buf, size_t maxLen) {
    auto it = store().find(fullKey(key));
    if (it == store().end()) return 0;
    size_t n = std::min(maxLen, it->second.size());
    memcpy(buf, it->second.data(), n);
    return n;
  }

  static void resetAll() { store().clear(); }

 private:
  std::string fullKey(const char *key) const { return ns_ + ":" + key; }
  static std::map<std::string, std::vector<uint8_t>> &store() {
    static std::map<std::string, std::vector<uint8_t>> s;
    return s;
  }
  std::string ns_;
};

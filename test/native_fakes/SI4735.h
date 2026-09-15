#pragma once

#include <cstdint>

#define FM_CURRENT_MODE 0
#define AM_CURRENT_MODE 1

// Minimal native stand-in for the PU2CLR SI4735 driver -- just enough of
// the API RadioTuner calls to test its own wrapper logic (band clamping,
// presets, sleep timer, transient-vs-persisted volume) without real I2C
// hardware. Records what was set and lets tests control what "the chip"
// reports back.
//
// getCurrentRSSI() reads a process-wide simulated value rather than
// per-instance state, since tests only ever have one RadioTuner alive at a
// time and this avoids needing a test-only accessor into RadioTuner just to
// reach its private SI4735 member.
class SI4735 {
 public:
  // Real library scans the I2C bus for the chip at 0x11/0x63 and returns 0
  // if neither responds -- RadioTuner::begin() uses that to detect whether
  // the module is actually wired up before touching anything else.
  int16_t getDeviceI2CAddress(uint8_t resetPin) {
    (void)resetPin;
    return simulatedPresent() ? 0x11 : 0;
  }
  static void setSimulatedPresent(bool present) { simulatedPresent() = present; }
  static void resetSimulatedPresent() { simulatedPresent() = true; }  // default: chip present

  void setup(uint8_t resetPin, uint8_t defaultFunction) {
    (void)resetPin;
    (void)defaultFunction;
  }
  void setFM(uint16_t fromFreq, uint16_t toFreq, uint16_t initialFreq, uint16_t step) {
    lastFmBandStart() = fromFreq;
    lastFmBandEnd() = toFreq;
    lastFmStep() = step;
    frequency = initialFreq;
    driverCallCount()++;
  }
  void setFrequency(uint16_t freq) {
    frequency = freq;
    driverCallCount()++;
  }
  uint16_t getFrequency() { return frequency; }

  // Process-wide, same rationale as simulatedRssi()/driverCallCount() --
  // only one RadioTuner (and its one SI4735) is ever alive in a test at a
  // time, and this is what applyRegion()-driven tests check to confirm a
  // region's values actually reached the driver, not just that
  // RadioTuner's own state changed.
  void setFMDeEmphasis(uint8_t parameter) {
    lastFmDeEmphasis() = parameter;
    driverCallCount()++;
  }
  static uint8_t lastAppliedFmDeEmphasis() { return lastFmDeEmphasis(); }
  static uint16_t lastAppliedFmBandStart() { return lastFmBandStart(); }
  static uint16_t lastAppliedFmBandEnd() { return lastFmBandEnd(); }

  void setRdsConfig(uint8_t rdsen, uint8_t bletha, uint8_t blethb, uint8_t blethc, uint8_t blethd) {
    (void)bletha;
    (void)blethb;
    (void)blethc;
    (void)blethd;
    rdsEnabled = rdsen != 0;
  }
  void setFifoCount(uint8_t count) { (void)count; }

  // rdsBeginQuery()+getRdsDateTime() mirror the real library's two-step
  // query: the first "receives" whatever CT frame a test has queued (see
  // setSimulatedRdsDateTime()), the second decodes it. A simulated frame
  // persists until explicitly cleared, same as a real station that keeps
  // repeating its CT group -- tests control exactly when it "arrives" and
  // stops.
  void rdsBeginQuery() { rdsQueryCalls++; }
  bool getRdsDateTime(uint16_t *year, uint16_t *month, uint16_t *day, uint16_t *hour, uint16_t *minute) {
    if (!simulatedRdsPresent()) return false;
    *year = simulatedRdsYear();
    *month = simulatedRdsMonth();
    *day = simulatedRdsDay();
    *hour = simulatedRdsHour();
    *minute = simulatedRdsMinute();
    return true;
  }

  static void setSimulatedRdsDateTime(uint16_t year, uint16_t month, uint16_t day, uint16_t hour,
                                       uint16_t minute) {
    simulatedRdsPresent() = true;
    simulatedRdsYear() = year;
    simulatedRdsMonth() = month;
    simulatedRdsDay() = day;
    simulatedRdsHour() = hour;
    simulatedRdsMinute() = minute;
  }
  static void clearSimulatedRdsDateTime() { simulatedRdsPresent() = false; }

  void setVolume(uint8_t v) {
    volume = v;
    driverCallCount()++;
  }

  // Real library: getCurrentRSSI()/getCurrentSNR() just return fields this
  // call populates. The fake tracks it as a real driver call so a test can
  // confirm RadioTuner::rssi()/snr() actually query fresh, rather than
  // trusting a value nothing ever populated -- but the values themselves
  // come from simulatedRssi()/simulatedSnr() (or the frequency-specific
  // override below), not anything this call itself sets.
  void getCurrentReceivedSignalQuality() { driverCallCount()++; }
  uint8_t getCurrentRSSI() {
    int i = findOverride(frequency);
    return i >= 0 ? overrideRssi()[i] : simulatedRssi();
  }
  uint8_t getCurrentSNR() {
    int i = findOverride(frequency);
    return i >= 0 ? overrideSnr()[i] : simulatedSnr();
  }
  void setAudioMute(bool m) {
    muted = m;
    driverCallCount()++;
  }

  static void setSimulatedRssi(uint8_t value) { simulatedRssi() = value; }
  static void resetSimulatedRssi() { simulatedRssi() = 50; }  // default: "good signal"
  static void setSimulatedSnr(uint8_t value) { simulatedSnr() = value; }
  static void resetSimulatedSnr() { simulatedSnr() = 20; }  // default: "clean signal"

  // Simulates specific frequencies reading differently from
  // simulatedRssi()/simulatedSnr() everywhere else -- e.g. a station's
  // whole response curve (a weaker shoulder next to its actual peak), for
  // testing RadioTuner's software seek (see seekUp()/seekDown()/
  // climbToLocalPeak()), which settles at each candidate frequency in turn
  // and reads whatever's "there". Up to kMaxOverrides points at once;
  // setting the same frequency again replaces its values.
  static constexpr int kMaxOverrides = 8;
  static void setSimulatedSignalAt(uint16_t freq10kHz, uint8_t rssiValue, uint8_t snrValue) {
    int i = findOverride(freq10kHz);
    if (i < 0) {
      if (overrideCount() >= kMaxOverrides) return;
      i = overrideCount()++;
      overrideFreq()[i] = freq10kHz;
    }
    overrideRssi()[i] = rssiValue;
    overrideSnr()[i] = snrValue;
  }
  static void clearSimulatedSignalAt() { overrideCount() = 0; }

  // Process-wide count of calls into any driver method that would talk to
  // real hardware (setFrequency/setVolume/setAudioMute/
  // getCurrentReceivedSignalQuality) -- lets a test confirm a guarded
  // RadioTuner method never reached the driver at all, not just that it
  // didn't crash.
  static int &driverCallCount() {
    static int v = 0;
    return v;
  }
  static void resetDriverCallCount() { driverCallCount() = 0; }

  // Test-observable state.
  uint16_t frequency = 0;
  uint8_t volume = 0;
  bool muted = false;
  bool rdsEnabled = false;
  int rdsQueryCalls = 0;

 private:
  static uint8_t &simulatedRssi() {
    static uint8_t v = 50;
    return v;
  }
  static uint8_t &simulatedSnr() {
    static uint8_t v = 20;
    return v;
  }
  static bool &simulatedPresent() {
    static bool v = true;
    return v;
  }
  static uint8_t &lastFmDeEmphasis() {
    static uint8_t v = 0;
    return v;
  }
  static uint16_t &lastFmBandStart() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &lastFmBandEnd() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &lastFmStep() {
    static uint16_t v = 0;
    return v;
  }
  static int &overrideCount() {
    static int v = 0;
    return v;
  }
  static uint16_t *overrideFreq() {
    static uint16_t v[kMaxOverrides];
    return v;
  }
  static uint8_t *overrideRssi() {
    static uint8_t v[kMaxOverrides];
    return v;
  }
  static uint8_t *overrideSnr() {
    static uint8_t v[kMaxOverrides];
    return v;
  }
  // -1 if freq has no override set.
  static int findOverride(uint16_t freq) {
    for (int i = 0; i < overrideCount(); i++) {
      if (overrideFreq()[i] == freq) return i;
    }
    return -1;
  }
  static bool &simulatedRdsPresent() {
    static bool v = false;
    return v;
  }
  static uint16_t &simulatedRdsYear() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &simulatedRdsMonth() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &simulatedRdsDay() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &simulatedRdsHour() {
    static uint16_t v = 0;
    return v;
  }
  static uint16_t &simulatedRdsMinute() {
    static uint16_t v = 0;
    return v;
  }
};

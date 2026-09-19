#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <SI4735.h>

#include "Config.h"
#include "RegionStore.h"

// Thin wrapper around the SI4735 driver for the SI4730 FM tuner: adds preset
// storage/persistence and clamps everything to the FM band so callers don't
// have to poke the underlying library's raw 10 kHz units directly.
class RadioTuner {
 public:
  // region is read for FM de-emphasis/band/step at begin() and whenever
  // applyRegion() is called again after the region changes (see
  // RegionStore) -- RadioTuner holds a reference rather than owning it,
  // same as MenuSystem holding a TimezoneStore&.
  explicit RadioTuner(RegionStore &region) : region_(region) {}

  // Returns false if no SI4730/35 responded on the I2C bus at either its
  // known address -- e.g. the module isn't wired up yet. Every method
  // below checks available() internally and no-ops (or returns a zeroed
  // default) rather than touching the underlying driver when it's false --
  // the PU2CLR SI4735 library's waitToSend() polls the chip's Clear-To-Send
  // bit in an unbounded loop with no timeout, so calling into it with no
  // chip actually present hangs the calling task forever (this is exactly
  // what used to happen when the web dashboard queried radio state
  // unconditionally on every poll: an infinite busy-loop with no chip to
  // ever set that bit, eventually tripping whichever task's watchdog was
  // waiting on it). Callers still don't need to check available() first --
  // it's just for deciding whether to show/skip radio UI.
  bool begin(uint8_t resetPin = Pins::RadioReset);
  bool available() const { return available_; }

  void tune(uint16_t frequency10kHz);
  // One region-step (RegionEntry::fmStep) up/down within the current
  // region's band, wrapping around at the edges -- stepping down from the
  // band minimum lands on the topmost frequency actually *on* the grid
  // (not always the same as the band's own upper bound; see
  // RadioTuner.cpp's topOfGrid()), and vice versa -- what the Radio
  // screen's held up/down buttons use. tune() itself still just clamps for
  // absolute sets (presets, dashboard, region re-clamp), where jumping to
  // the opposite end of the band would be surprising rather than useful.
  void stepUp();
  void stepDown();
  // Starts a software seek: steps through the band one fmStep at a time
  // (wrapping at the edges), settling briefly at each candidate and
  // checking Sig/SNR against Config.h's SeekRssiThreshold/SeekSnrThreshold
  // -- not the chip's own hardware seek. See the threshold constants'
  // comment in Config.h for why: the hardware seek's in-sweep reading
  // proved unreliable under marginal reception. Once a candidate clears
  // both thresholds it keeps stepping a little further to land on the
  // station's actual peak rather than its leading shoulder (a real
  // station's response is wider than one fmStep, so the first candidate to
  // clear the bar is rarely the strongest point). Ends back on the starting
  // frequency if nothing cleared the threshold anywhere in the band.
  //
  // Returns immediately -- the sweep itself is advanced one candidate per
  // SeekSettleMs by update() (call it every loop iteration), so nothing
  // holds the shared state lock for the whole sweep. It used to block for
  // the full pass: 103 steps x 30ms on Americas' grid, but ~206 on the
  // 100kHz grids -- over 6s, past the 5s task watchdog AsyncTCP's own task
  // runs under, so a dashboard request that merely *waited* on the lock
  // during a full sweep (or a seek started from the dashboard, which ran
  // the sweep on that task directly) rebooted the device. seeking() reports
  // whether one is in progress; tune() (and therefore stepUp()/stepDown()/
  // recallPreset()) cancels it.
  void seekUp();
  void seekDown();
  bool seeking() const { return seekPhase_ != SeekPhase::Idle; }

  void setVolume(uint8_t volume);  // 0-63, persisted (lazily -- see update())
  // Sets the volume without writing to flash -- for the sunrise ramp, which
  // would otherwise hit NVS every second.
  void setVolumeTransient(uint8_t volume);
  // The live value -- what's actually being sent to the chip right now,
  // which during a radio-wake ramp is the current (quiet, rising)
  // transient step, not the user's real setting. Used for on-screen
  // display and as the -1 base for volumeDown(), so that button feels like
  // it's adjusting what you're actually hearing. volumeUp() is the
  // exception while a ramp is holding the live value *below* the saved
  // one: it jumps straight to the saved volume (i.e. "skip the ramp")
  // rather than nudging the quiet transient step up by one and, worse,
  // saving that as the new real volume -- a single press used to turn a
  // saved 30 into a saved 5.
  uint8_t volume() const { return volume_; }
  // The last value actually requested via setVolume() (persisted to NVS) --
  // never touched by setVolumeTransient(), so it survives a sunrise ramp,
  // a snooze, or a dead-air fallback intact. This is what WakeController
  // ramps *toward*, specifically so repeatedly snoozing a radio alarm can't
  // ratchet the target down to whatever quiet step the ramp happened to be
  // on when it was interrupted.
  uint8_t persistedVolume() const { return persistedVolume_; }
  void volumeUp();
  void volumeDown();

  void setMuted(bool muted);
  bool muted() const { return muted_; }

  uint16_t frequency10kHz();
  float frequencyMHz() { return frequency10kHz() / 100.0f; }
  uint8_t rssi();
  uint8_t snr();  // dB, 0-127 -- same live query as rssi(), see RadioTuner.cpp

  uint8_t presetCount() const { return RadioConfig::MaxPresets; }
  uint16_t preset(uint8_t index) const { return presets_[index]; }
  void storePreset(uint8_t index, uint16_t frequency10kHz);
  void recallPreset(uint8_t index);

  // Pushes the current region's FM de-emphasis and band/step to the chip,
  // re-clamping the current frequency into the new band if needed. Called
  // once from begin(); call it again after changing region_'s selection
  // (RegionStore::setIndex() itself is just a persisted index, same as
  // TimezoneStore -- it doesn't know about RadioTuner or touch hardware).
  void applyRegion();

  // Sleep timer: mutes automatically once it elapses.
  void setSleepTimer(uint16_t minutes);
  void cancelSleepTimer();
  bool sleepTimerActive() const { return sleepTimerDurationMs_ != 0; }
  uint16_t sleepTimerRemainingMinutes() const;
  // Call every loop iteration: advances an in-progress seek (one candidate
  // per SeekSettleMs), expires the sleep timer, and flushes pending
  // volume/mute changes to NVS once they've been quiet for
  // SettingsFlushDelayMs (see that constant's comment for why not eagerly).
  void update();

  // RDS Clock Time (CT) fallback sync -- CURRENTLY DISABLED, see
  // updateRdsSync()'s definition in RadioTuner.cpp. The intended design
  // (still implemented, just short-circuited): call once a second from
  // main.cpp's slow tick; needsFallback is true when main.cpp/WebDashboard
  // have decided there's no reliable NTP time yet (RadioTuner intentionally
  // has no WebDashboard reference, so it can't decide this itself).
  // Whenever the radio is on and unmuted (the user is already listening),
  // this would harvest any CT group for free regardless of needsFallback.
  // Only when needsFallback is true AND the radio is currently muted/idle
  // would it periodically re-assert the last-used frequency (to (re)start
  // RDS acquisition) and listen for a CT group for a bounded window --
  // never while the radio is in active use, with any real user action
  // cancelling an in-progress attempt immediately (see
  // tune()/seekUp()/seekDown()/setMuted()). Disabled because the PU2CLR
  // SI4735 library call this needs (getRdsStatus(), reached via
  // rdsBeginQuery()/getRdsDateTime()) retries forever with no timeout on an
  // ERR status and never re-issues the command -- confirmed live to hang
  // the whole device (not just the radio) once that got stuck, which real
  // FM reception (weak signal, a non-RDS station) triggers easily.
  void updateRdsSync(bool needsFallback);
  // True exactly once after a plausible CT frame was decoded -- read
  // rdsTime() immediately after calling this.
  bool consumeRdsTimeSync();
  DateTime rdsTime() const { return rdsTime_; }

  // Station name (PS, up to 8 chars) and RadioText (up to 64 chars),
  // decoded from RDS groups 0/2 -- entirely independent of the disabled
  // Clock Time sync above (updateRdsSync()/pollRdsForTime()); this never
  // touches rdsBeginQuery()/getRdsDateTime() or si4735_'s own RDS methods
  // at all. Empty ("") until something's actually been decoded. Call
  // pollRdsText() once a second from main.cpp's slow tick; it no-ops
  // unless the radio is available and unmuted (passive harvesting only,
  // same spirit as the disabled fallback's "free while already
  // listening" half -- never retunes or interrupts anything).
  void pollRdsText();
  const char *stationName() const { return psName_; }
  const char *radioText() const { return radioText_; }

  // Pure decode: given a 13-byte FM_RDS_STATUS response, updates
  // psName_/radioText_. No Wire/SI4735 access at all -- public (rather
  // than an implementation detail of pollRdsText()) specifically so tests
  // can drive it directly with hand-built raw[13] arrays, without needing
  // a test-controllable Wire fake for logic that never touches Wire in
  // the first place. Decodes Block B with plain bit-shifts, not the
  // library's platform-dependent bitfield unions.
  void decodeRdsGroup(const uint8_t raw[13]);

 private:
  void applyVolume(uint8_t volume);
  // Drives Pins::AmpMute high (shunting the audio line to ground through the
  // external mute transistor) whenever muted_ or volume_ == 0, low
  // otherwise -- called from setMuted()/applyVolume() (and once from
  // begin(), before the chip-availability check, so a missing/unresponsive
  // chip still leaves the amp muted rather than floating). Independent of
  // available_ the rest of the way too: this is a plain GPIO, not I2C, so
  // there's no chip call to skip.
  void updateAmpMutePin();
  // Writes freq to the persisted tuning-frequency key -- shared by tune()
  // and by a successful seek (climbToLocalPeak()), which used to set the
  // chip's frequency directly without ever persisting it, so a seeked
  // station reverted to the last *tuned* one on reboot or a region switch.
  void persistFrequency(uint16_t freq);
  void markSettingsDirty();
  void save();
  void load();
  // Polls the RDS FIFO once; sets rdsTimeReady_ (and cancels an
  // in-progress fallback attempt, if any) on a plausible CT frame.
  void pollRdsForTime();
  // Issues the FM_RDS_STATUS command directly over Wire (using
  // i2cAddress_, captured at begin()) and reads the 13-byte response, with
  // a BOUNDED wait for CTS and a BOUNDED number of retries on an ERR
  // response -- unlike the PU2CLR SI4735 library's own getRdsStatus(),
  // which does both unbounded and is exactly what hung the whole device
  // once already (see updateRdsSync()'s comment). Returns false (raw left
  // untouched) if it gives up within those bounds; never blocks forever.
  bool readRdsGroupSafely(uint8_t raw[13]);

  // Seek state machine -- see seekUp(). Sweeping: stepping candidate by
  // candidate until one clears both thresholds. Climbing: that found, keep
  // stepping in the same direction while SNR keeps improving, then back up
  // to the best one seen -- a real station's response curve is wider than
  // one fmStep, so the first candidate to clear the bar is often its
  // leading shoulder, not the peak (confirmed against a live band sweep:
  // a station peaking at Sig 31/SNR 16 had a neighbor one step earlier
  // already at Sig 24/SNR 8, well past threshold). A simple local
  // hill-climb, not a second full sweep.
  enum class SeekPhase : uint8_t { Idle, Sweeping, Climbing };
  void startSeek(bool up);
  void advanceSeek();
  void finishSeek(uint16_t freq);
  uint16_t nextCandidate(uint16_t from, bool up) const;

  struct SignalQuality {
    uint8_t rssi;
    uint8_t snr;
  };
  // One I2C query (getCurrentReceivedSignalQuality()) populates both RSSI
  // and SNR -- this reads both from that single query, for the seek loops
  // (which always want both from the same instant). The public rssi()/
  // snr() stay independent single-value queries each -- see
  // test_rssi_queries_fresh_signal_quality_each_call -- since the TFT and
  // dashboard read them separately, often seconds apart; this is purely an
  // internal shortcut for seekUp()/seekDown()/climbToLocalPeak(), which
  // used to call rssi() then snr() back to back and pay for the same
  // underlying query twice per candidate.
  SignalQuality readSignalQuality();

  SI4735 si4735_;
  RegionStore &region_;
  bool available_ = false;
  uint8_t volume_ = RadioConfig::DefaultVolume;
  uint8_t persistedVolume_ = RadioConfig::DefaultVolume;
  bool muted_ = false;
  uint16_t presets_[RadioConfig::MaxPresets] = {};
  // Sleep timer stored as start+duration, not a precomputed absolute
  // deadline -- millis() + duration can overflow uint32_t and wrap to a
  // small value near the ~49.7-day millis() rollover, which a plain
  // millis() >= deadline comparison then reads as "already expired"
  // hours early. Comparing elapsed = millis() - start against duration
  // instead relies on unsigned-subtraction wraparound, which stays
  // correct across that rollover as long as the timer itself is shorter
  // than ~49.7 days (MaxSleepTimerMinutes is 120).
  uint32_t sleepTimerStartMs_ = 0;
  uint32_t sleepTimerDurationMs_ = 0;  // 0 = inactive

  // Set by setVolume()/setMuted(); update() writes NVS once it's been
  // SettingsFlushDelayMs since the last change.
  bool settingsDirty_ = false;
  uint32_t settingsChangedMs_ = 0;

  SeekPhase seekPhase_ = SeekPhase::Idle;
  bool seekUp_ = true;
  uint16_t seekStartFreq_ = 0;     // where to return to if nothing's found
  uint16_t seekCandidate_ = 0;     // the frequency currently settling on the chip
  uint16_t seekStepsLeft_ = 0;     // bounded to one full pass of the band
  uint16_t seekBestFreq_ = 0;      // Climbing: best so far
  uint8_t seekBestSnr_ = 0;
  uint32_t seekStepMs_ = 0;        // when seekCandidate_ was set, for the settle wait

  bool rdsFallbackActive_ = false;
  uint32_t rdsFallbackStartMs_ = 0;
  uint32_t lastRdsFallbackAttemptMs_ = 0;  // 0 = never attempted yet this boot
  bool rdsTimeReady_ = false;
  DateTime rdsTime_;

  // Captured once in begin() (getDeviceI2CAddress() already found this) so
  // readRdsGroupSafely() can talk to the chip directly without needing
  // si4735_ to expose its own private address.
  uint8_t i2cAddress_ = 0;
  char psName_[9] = {};       // 8-char PS name + NUL
  char radioText_[65] = {};   // up to 64-char RadioText + NUL
  bool radioTextAbFlag_ = false;
  bool radioTextAbFlagKnown_ = false;
  // Frequency last seen by pollRdsText() -- a change means a different
  // station, so the previous station's name/RadioText no longer apply.
  // Checked here (not at every tune()/seekUp()/etc. call site) since some
  // of those set the frequency directly on si4735_ rather than through a
  // single shared path.
  uint16_t lastRdsFrequency_ = 0;
};

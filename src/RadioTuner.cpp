#include "RadioTuner.h"

#include <cstring>

#include <Preferences.h>
#include <Wire.h>

namespace {
constexpr const char *kNamespace = "radio";
constexpr const char *kVolumeKey = "volume";
constexpr const char *kFreqKey = "freq";
constexpr const char *kMutedKey = "muted";

// FM_RDS_STATUS command byte (Si47XX PROGRAMMING GUIDE AN332) -- not
// exposed by the PU2CLR SI4735 library's public API, so it's redefined
// here rather than reaching into the library's own private header value.
constexpr uint8_t kFmRdsStatusCommand = 0x24;

// Rounds freq to the nearest point on the region's channel grid
// (fmBandStart + k*fmStep), clamped back into the band if rounding would
// push past its edge. A no-op whenever freq is already on-grid -- true for
// every region except Americas, whose real 200kHz channel spacing means a
// manually-entered or old-preset frequency landing on an "even" tenth
// needs correcting (see RadioTuner::tune()'s own comment for why this
// isn't optional).
uint16_t snapToGrid(uint16_t freq, const RegionEntry &r) {
  uint16_t offset = (freq - r.fmBandStart) % r.fmStep;
  if (offset == 0) return freq;
  uint16_t roundedUp = freq + (r.fmStep - offset);
  bool nearerUp = offset * 2 >= r.fmStep;
  if (nearerUp && roundedUp <= r.fmBandEnd) return roundedUp;
  return freq - offset;
}

// The highest frequency on the region's grid that's still <= fmBandEnd --
// not necessarily fmBandEnd itself. Americas' band ends at 108.0MHz, but
// its topmost real channel (grid-aligned from fmBandStart) is 107.9MHz.
uint16_t topOfGrid(const RegionEntry &r) {
  uint16_t span = r.fmBandEnd - r.fmBandStart;
  return r.fmBandStart + (span / r.fmStep) * r.fmStep;
}

// RDS RadioText's own end-of-message marker (0x0D) -- stations shorter
// than the full 64/32-char buffer terminate with this rather than padding
// the rest with spaces. Mapped to a real NUL so radioText() (read as a
// plain C-string) stops there instead of showing a raw control character
// followed by whatever trails it in the buffer.
char rdsChar(uint8_t byte) { return byte == 0x0D ? '\0' : (char)byte; }
}  // namespace

bool RadioTuner::begin(uint8_t resetPin) {
  load();

  pinMode(Pins::AmpMute, OUTPUT);
  updateAmpMutePin();  // reflect the persisted mute/volume state right away,
                        // even if the chip below never responds

  int16_t address = si4735_.getDeviceI2CAddress(resetPin);
  available_ = address != 0;
  if (!available_) return false;
  i2cAddress_ = (uint8_t)address;

  si4735_.setup(resetPin, FM_CURRENT_MODE);
  applyRegion();  // sets de-emphasis + band, tunes to the persisted frequency
  si4735_.setVolume(volume_);
  si4735_.setAudioMute(muted_);  // the chip itself powers up unmuted -- apply the persisted state
  // RDS block-error tolerance -- 1/2/2/2/2 (max errors allowed per block
  // before it's discarded) is the PU2CLR SI4735 library's own example
  // value, not independently tuned; fine for a bedside-clock-grade CT
  // fallback where an occasional dropped group just means trying again a
  // second later. FIFO count 1 means an interrupt/ready condition is
  // signaled as soon as a single group arrives -- polled explicitly here
  // rather than via interrupt, so this just controls FIFO depth.
  si4735_.setRdsConfig(1, 2, 2, 2, 2);
  si4735_.setFifoCount(1);
  return true;
}

void RadioTuner::applyRegion() {
  if (!available_) return;
  const RegionEntry &r = region_.current();
  si4735_.setFMDeEmphasis(r.fmDeEmphasis);

  // Re-clamp the persisted frequency into the new region's band -- e.g.
  // switching from Americas/Europe (87.5-108.0MHz) to Japan
  // (76.0-95.0MHz) can otherwise leave the chip tuned outside its own
  // configured band.
  Preferences prefs;
  prefs.begin(kNamespace, true);
  uint16_t storedFreq = prefs.getUShort(kFreqKey, RadioConfig::FmDefaultFreq);
  prefs.end();
  uint16_t freq = constrain(storedFreq, r.fmBandStart, r.fmBandEnd);
  freq = snapToGrid(freq, r);
  // Write the re-clamped/snapped value back -- otherwise NVS keeps holding
  // the pre-switch frequency, which the next applyRegion() (another region
  // switch, or the next boot) would read back and re-clamp from scratch
  // rather than from where the chip actually ended up.
  if (freq != storedFreq) persistFrequency(freq);

  si4735_.setFM(r.fmBandStart, r.fmBandEnd, freq, r.fmStep);
}

void RadioTuner::tune(uint16_t frequency10kHz) {
  rdsFallbackActive_ = false;  // real user action cancels a background sync attempt
  seekPhase_ = SeekPhase::Idle;  // ...and any seek still sweeping
  const RegionEntry &r = region_.current();
  frequency10kHz = constrain(frequency10kHz, r.fmBandStart, r.fmBandEnd);
  // Snap onto the region's channel grid -- matters for Americas' 200kHz-
  // spaced odd-decimal grid (88.1, 88.3, ...): a manual dashboard entry or
  // an old preset saved before this existed could otherwise land on an
  // invalid "even" frequency, which would then throw off every subsequent
  // step/seek too, since those just add/subtract fmStep from wherever the
  // radio currently sits -- this is what keeps that self-correcting instead.
  frequency10kHz = snapToGrid(frequency10kHz, r);
  // Persisted either way (matches volume_/muted_ below) so it's already in
  // place for whenever a chip does get connected -- only the actual
  // hardware write is skipped without one.
  persistFrequency(frequency10kHz);
  if (!available_) return;
  si4735_.setFrequency(frequency10kHz);
}

void RadioTuner::persistFrequency(uint16_t freq) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUShort(kFreqKey, freq);
  prefs.end();
}

void RadioTuner::stepUp() {
  const RegionEntry &r = region_.current();
  uint16_t current = frequency10kHz();
  uint16_t next = current + r.fmStep;
  tune(next > r.fmBandEnd ? r.fmBandStart : next);
}

void RadioTuner::stepDown() {
  const RegionEntry &r = region_.current();
  uint16_t current = frequency10kHz();
  // Wraps to the actual topmost grid channel, not fmBandEnd itself -- see
  // topOfGrid()'s comment. Checked before subtracting since these are
  // unsigned: current - fmStep would underflow if current is already at
  // (or within one step of) the band's bottom edge.
  tune(current < r.fmBandStart + r.fmStep ? topOfGrid(r) : current - r.fmStep);
}

void RadioTuner::seekUp() { startSeek(/*up=*/true); }
void RadioTuner::seekDown() { startSeek(/*up=*/false); }

// The candidate after `from`, wrapping at the band edges -- downward wraps
// to the topmost grid channel, not fmBandEnd itself (see topOfGrid()).
uint16_t RadioTuner::nextCandidate(uint16_t from, bool up) const {
  const RegionEntry &r = region_.current();
  if (up) {
    uint16_t next = from + r.fmStep;
    return next > r.fmBandEnd ? r.fmBandStart : next;
  }
  return from < r.fmBandStart + r.fmStep ? topOfGrid(r) : from - r.fmStep;
}

void RadioTuner::startSeek(bool up) {
  rdsFallbackActive_ = false;
  if (!available_) return;
  const RegionEntry &r = region_.current();
  seekUp_ = up;
  seekStartFreq_ = frequency10kHz();
  // Bounded to one full pass of the band (never less than 1, even if fmStep
  // somehow didn't divide it evenly) so this can't sweep forever.
  seekStepsLeft_ = (r.fmBandEnd - r.fmBandStart) / r.fmStep + 1;
  seekCandidate_ = nextCandidate(seekStartFreq_, up);
  seekStepsLeft_--;
  si4735_.setFrequency(seekCandidate_);
  seekStepMs_ = millis();
  seekPhase_ = SeekPhase::Sweeping;
}

void RadioTuner::advanceSeek() {
  if (millis() - seekStepMs_ < RadioConfig::SeekSettleMs) return;  // still settling
  const RegionEntry &r = region_.current();
  SignalQuality sq = readSignalQuality();

  if (seekPhase_ == SeekPhase::Sweeping) {
    if (sq.rssi >= RadioConfig::SeekRssiThreshold && sq.snr >= RadioConfig::SeekSnrThreshold) {
      seekBestFreq_ = seekCandidate_;
      seekBestSnr_ = sq.snr;
      seekPhase_ = SeekPhase::Climbing;
      // Fall through to the first climb step below.
    } else if (seekStepsLeft_ == 0) {
      // Nothing in the whole band cleared the threshold -- back to where we
      // started, and nothing to persist since nothing changed.
      si4735_.setFrequency(seekStartFreq_);
      seekPhase_ = SeekPhase::Idle;
      return;
    } else {
      seekCandidate_ = nextCandidate(seekCandidate_, seekUp_);
      seekStepsLeft_--;
      si4735_.setFrequency(seekCandidate_);
      seekStepMs_ = millis();
      return;
    }
  } else if (sq.snr <= seekBestSnr_) {
    // Climbing, and the signal is falling off again -- the previous
    // candidate was the peak.
    finishSeek(seekBestFreq_);
    return;
  } else {
    seekBestFreq_ = seekCandidate_;
    seekBestSnr_ = sq.snr;
  }

  // Climbing: one more step in the same direction, without wrapping.
  uint16_t next = seekUp_ ? seekBestFreq_ + r.fmStep : seekBestFreq_ - r.fmStep;
  if (next < r.fmBandStart || next > r.fmBandEnd) {
    finishSeek(seekBestFreq_);  // band edge -- don't wrap mid-climb
    return;
  }
  seekCandidate_ = next;
  si4735_.setFrequency(seekCandidate_);
  seekStepMs_ = millis();
}

void RadioTuner::finishSeek(uint16_t freq) {
  si4735_.setFrequency(freq);
  // A successful seek lands here, not through tune() -- persist it too, or
  // it reverts to the last explicitly-tuned frequency on reboot or a
  // region switch (which re-clamps from the persisted value, not the chip).
  persistFrequency(freq);
  seekPhase_ = SeekPhase::Idle;
}

void RadioTuner::setVolume(uint8_t volume) {
  applyVolume(volume);
  persistedVolume_ = volume_;
  markSettingsDirty();
}

void RadioTuner::markSettingsDirty() {
  settingsDirty_ = true;
  settingsChangedMs_ = millis();
}

void RadioTuner::setVolumeTransient(uint8_t volume) { applyVolume(volume); }

void RadioTuner::applyVolume(uint8_t volume) {
  volume_ = min<uint8_t>(volume, 63);
  updateAmpMutePin();
  if (!available_) return;
  si4735_.setVolume(volume_);
}

void RadioTuner::volumeUp() {
  // Mid-ramp (live value held below the saved one by setVolumeTransient()):
  // jump to the saved volume rather than +1 -- see volume()'s comment.
  if (volume_ < persistedVolume_) {
    setVolume(persistedVolume_);
    return;
  }
  setVolume(min<uint8_t>(volume_ + 1, 63));
}
void RadioTuner::volumeDown() { setVolume(volume_ > 0 ? volume_ - 1 : 0); }

void RadioTuner::updateAmpMutePin() {
  digitalWrite(Pins::AmpMute, (muted_ || volume_ == 0) ? HIGH : LOW);
}

void RadioTuner::setMuted(bool muted) {
  if (!muted) rdsFallbackActive_ = false;  // unmuting means the user wants to listen now
  muted_ = muted;
  updateAmpMutePin();
  markSettingsDirty();  // so the radio doesn't wake up audible after a reboot the user left it muted before
  if (!available_) return;
  si4735_.setAudioMute(muted_);
}

uint16_t RadioTuner::frequency10kHz() {
  if (!available_) return 0;
  return si4735_.getFrequency();
}
uint8_t RadioTuner::rssi() { return readSignalQuality().rssi; }
uint8_t RadioTuner::snr() { return readSignalQuality().snr; }

RadioTuner::SignalQuality RadioTuner::readSignalQuality() {
  if (!available_) return {0, 0};
  // getCurrentRSSI()/getCurrentSNR() alone just return cached fields --
  // nothing populates them without this call first (this is why the Radio
  // screen's "Sig" line always read 0: RadioTuner never made this call at
  // all).
  si4735_.getCurrentReceivedSignalQuality();
  return {si4735_.getCurrentRSSI(), si4735_.getCurrentSNR()};
}

void RadioTuner::storePreset(uint8_t index, uint16_t frequency10kHz) {
  if (index >= presetCount()) return;
  presets_[index] = frequency10kHz;

  Preferences prefs;
  prefs.begin(kNamespace, false);
  char key[8];
  snprintf(key, sizeof(key), "p%u", index);
  prefs.putUShort(key, frequency10kHz);
  prefs.end();
}

void RadioTuner::recallPreset(uint8_t index) {
  if (index >= presetCount() || presets_[index] == 0) return;
  tune(presets_[index]);
}

void RadioTuner::setSleepTimer(uint16_t minutes) {
  if (minutes == 0) {
    // Callers are expected to route 0 to cancelSleepTimer() themselves, but
    // an int-to-uint16_t truncation upstream (e.g. the dashboard casting a
    // JSON value of 65536) can land here with 0 anyway. Without this guard,
    // millis() + 0 is still a nonzero "deadline" already in the past, so the
    // very next update() would mute the radio immediately instead of
    // leaving the sleep timer inactive.
    cancelSleepTimer();
    return;
  }
  minutes = min(minutes, RadioConfig::MaxSleepTimerMinutes);
  sleepTimerStartMs_ = millis();
  sleepTimerDurationMs_ = (uint32_t)minutes * 60000UL;
}

void RadioTuner::cancelSleepTimer() { sleepTimerDurationMs_ = 0; }

uint16_t RadioTuner::sleepTimerRemainingMinutes() const {
  if (sleepTimerDurationMs_ == 0) return 0;
  uint32_t elapsedMs = millis() - sleepTimerStartMs_;  // wraps correctly across a millis() rollover
  if (elapsedMs >= sleepTimerDurationMs_) return 0;
  // Ceiling division: exactly N minutes left reads as N, not N+1.
  return (sleepTimerDurationMs_ - elapsedMs + 60000UL - 1) / 60000UL;
}

void RadioTuner::update() {
  if (seekPhase_ != SeekPhase::Idle) advanceSeek();
  if (sleepTimerDurationMs_ != 0 && millis() - sleepTimerStartMs_ >= sleepTimerDurationMs_) {
    setMuted(true);
    sleepTimerDurationMs_ = 0;
  }
  if (settingsDirty_ && millis() - settingsChangedMs_ >= RadioConfig::SettingsFlushDelayMs) {
    settingsDirty_ = false;
    save();
  }
}

void RadioTuner::pollRdsForTime() {
  if (rdsTimeReady_) return;  // unconsumed result already waiting -- don't overwrite it
  si4735_.rdsBeginQuery();
  uint16_t year, month, day, hour, minute;
  if (!si4735_.getRdsDateTime(&year, &month, &day, &hour, &minute)) return;
  // The library's own getRdsDateTime() already rejects hour>24, minute>60,
  // day>31, month>12 -- year is the one implausible case it doesn't check,
  // and RDS data (or the MJD-to-calendar conversion) can be noisy.
  if (year < RadioConfig::MinPlausibleRdsYear || year > RadioConfig::MaxPlausibleRdsYear) return;
  rdsTime_ = DateTime(year, month, day, hour, minute, 0);
  rdsTimeReady_ = true;
  rdsFallbackActive_ = false;  // got what we needed
}

void RadioTuner::updateRdsSync(bool needsFallback) {
  // Disabled -- see the class comment on updateRdsSync() in RadioTuner.h.
  // The PU2CLR SI4735 library's getRdsStatus() (reached via
  // rdsBeginQuery()/getRdsDateTime(), called from pollRdsForTime() below)
  // retries forever on an ERR status with no timeout and never re-issues
  // the command, so once ERR sticks -- easy to hit with a real station on
  // a weak signal or one that doesn't broadcast RDS at all -- it hangs
  // loop() permanently (confirmed live: the whole clock/alarm/menu froze,
  // not just the radio). Left in place, inert, rather than ripped out, in
  // case a bounded-retry patch to the vendored library makes it safe to
  // re-enable later.
  (void)needsFallback;
  return;

#if 0  // kept for reference -- see the disabled-return above
  if (!available_) return;

  if (!muted_) {
    // Radio is in active use -- passively harvest any CT group for free
    // (reading data that's already flowing), but never start or continue
    // a background retune attempt over what the user is listening to.
    rdsFallbackActive_ = false;
    pollRdsForTime();
    return;
  }

  if (rdsFallbackActive_) {
    pollRdsForTime();
    if (rdsTimeReady_ || millis() - rdsFallbackStartMs_ >= RadioConfig::RdsFallbackWindowMs) {
      rdsFallbackActive_ = false;
    }
    return;
  }

  if (!needsFallback) return;
  // 0 means "never attempted this boot" -- fire the first attempt right
  // away rather than waiting out a full interval first.
  if (lastRdsFallbackAttemptMs_ != 0 &&
      millis() - lastRdsFallbackAttemptMs_ < RadioConfig::RdsFallbackIntervalMs) {
    return;
  }

  lastRdsFallbackAttemptMs_ = millis();
  rdsFallbackStartMs_ = millis();
  rdsFallbackActive_ = true;
  // Already muted, so re-asserting the frequency -- which restarts the
  // chip's RDS group-sync acquisition -- produces no audible change.
  si4735_.setFrequency(si4735_.getFrequency());
#endif
}

bool RadioTuner::consumeRdsTimeSync() {
  bool ready = rdsTimeReady_;
  rdsTimeReady_ = false;
  return ready;
}

void RadioTuner::pollRdsText() {
  if (!available_ || muted_) return;

  uint16_t freq = frequency10kHz();
  if (freq != lastRdsFrequency_) {
    // Different station (however the frequency got here -- tune(), a
    // step, a seek, or a preset all end up here) -- its name/RadioText no
    // longer apply. Full clear, not just index 0 -- a partial clear left
    // the new station's first couple of characters followed by up to 62
    // stale characters from the *previous* station until every segment of
    // the new one happened to arrive.
    lastRdsFrequency_ = freq;
    memset(psName_, 0, sizeof(psName_));
    memset(radioText_, 0, sizeof(radioText_));
    radioTextAbFlagKnown_ = false;
  }

  // Drains the whole FIFO each poll rather than one group/call -- RDS
  // groups arrive at ~11.4/s over the air but this is only called once/sec
  // from main.cpp's slow tick, and the chip's FIFO is only ~25 groups deep,
  // so one group/poll badly undersamples RadioText (16 segments, spread
  // thin among all the other group types a real broadcast also sends).
  // Bounded to RdsMaxGroupsPerPoll so a chip that kept reporting more
  // pending forever couldn't turn this into an unbounded loop -- same
  // spirit as readRdsGroupSafely()'s own bounded retries.
  for (uint8_t i = 0; i < RadioConfig::RdsMaxGroupsPerPoll; i++) {
    uint8_t raw[13];
    if (!readRdsGroupSafely(raw)) return;  // gave up within bounds -- try again next second
    decodeRdsGroup(raw);
    if (raw[3] == 0) return;  // FIFO reports nothing more pending -- done for this poll
  }
}

bool RadioTuner::readRdsGroupSafely(uint8_t raw[13]) {
  Wire.beginTransmission(i2cAddress_);
  Wire.write(kFmRdsStatusCommand);
  Wire.write((uint8_t)0);  // INTACK=0, MTFIFO=0, STATUSONLY=0 -- a plain poll
  Wire.endTransmission();

  for (uint8_t attempt = 0; attempt < RadioConfig::RdsMaxErrRetries; attempt++) {
    bool ctsReady = false;
    for (uint8_t poll = 0; poll < RadioConfig::RdsMaxCtsPolls; poll++) {
      delayMicroseconds(RadioConfig::RdsCtsPollDelayUs);
      Wire.requestFrom(i2cAddress_, (uint8_t)1);
      if (Wire.available() && (Wire.read() & 0x80)) {  // bit 7 = CTS
        ctsReady = true;
        break;
      }
    }
    if (!ctsReady) return false;  // chip never asserted CTS -- give up, don't hang

    Wire.requestFrom(i2cAddress_, (uint8_t)13);
    for (uint8_t i = 0; i < 13; i++) raw[i] = Wire.available() ? Wire.read() : 0;
    if (!(raw[0] & 0x40)) return true;  // bit 6 = ERR, clear -- good read
  }
  return false;  // stuck ERR after bounded retries -- give up cleanly, try again next poll
}

void RadioTuner::decodeRdsGroup(const uint8_t raw[13]) {
  // Byte offsets match the FM_RDS_STATUS response (Si47XX PROGRAMMING
  // GUIDE AN332): [0]=status/ERR/CTS, [1..2]=RDS status flags,
  // [3]=FIFO used, [4..5]=Block A, [6..7]=Block B, [8..9]=Block C,
  // [10..11]=Block D, [12]=block error counts. Decoded with plain
  // bit-shifts rather than the library's bitfield unions -- simpler to
  // verify against the spec and has no platform-dependent bit-order risk.
  uint16_t blockB = ((uint16_t)raw[6] << 8) | raw[7];
  uint8_t blockCHigh = raw[8], blockCLow = raw[9];
  uint8_t blockDHigh = raw[10], blockDLow = raw[11];

  // RESP12: 2-bit correction level per block, 3 = uncorrectable. Declared
  // in this order (BLED first) so the first-declared field packs into the
  // lowest bits on this compiler/platform, matching AN332's own low-to-high
  // BLED/BLEC/BLEB/BLEA layout.
  uint8_t bled = raw[12] & 0x03;
  uint8_t blec = (raw[12] >> 2) & 0x03;
  uint8_t bleb = (raw[12] >> 4) & 0x03;

  uint8_t groupType = (blockB >> 12) & 0x0F;
  bool isVersionB = (blockB >> 11) & 0x01;

  // Block B carries the group type/segment address this whole decode keys
  // off of, and Block D supplies characters in every case below -- an
  // uncorrectable read of either means raw[6..7]/[10..11] can't be trusted
  // at all, so skip rather than write noise into the buffer where a
  // legitimate segment then has to wait a full RDS cycle to overwrite it.
  if (bleb == 3 || bled == 3) return;

  if (groupType == 0) {
    // Group 0A/0B: PS (station) name. Distribution is identical for both
    // versions -- Block D always carries 2 characters of the 8-char name,
    // for whichever of the 4 segments this group's address selects.
    uint8_t segment = blockB & 0x03;
    psName_[segment * 2] = rdsChar(blockDHigh);
    psName_[segment * 2 + 1] = rdsChar(blockDLow);
    psName_[8] = '\0';
  } else if (groupType == 2) {
    // Group 2A/2B: RadioText. A flip of the text A/B flag means a
    // genuinely new message -- clear the whole buffer before this segment
    // lands (not just index 0: a stale tail from the previous message
    // would otherwise sit past whatever this one's segments overwrite
    // until every one of its own 16 segments happens to arrive), same as
    // the library's own PS-name handling clears on a change.
    bool abFlag = (blockB >> 4) & 0x01;
    if (!radioTextAbFlagKnown_ || abFlag != radioTextAbFlag_) {
      memset(radioText_, 0, sizeof(radioText_));
      radioTextAbFlag_ = abFlag;
      radioTextAbFlagKnown_ = true;
    }
    uint8_t segment = blockB & 0x0F;
    if (!isVersionB) {
      // 2A: 4 chars/segment (Block C + Block D), 16 segments, 64 chars.
      // Block C is only used here -- only this branch needs to check it.
      if (blec == 3) return;
      radioText_[segment * 4] = rdsChar(blockCHigh);
      radioText_[segment * 4 + 1] = rdsChar(blockCLow);
      radioText_[segment * 4 + 2] = rdsChar(blockDHigh);
      radioText_[segment * 4 + 3] = rdsChar(blockDLow);
      radioText_[64] = '\0';
    } else {
      // 2B: 2 chars/segment (Block D only), 16 segments, 32 chars.
      radioText_[segment * 2] = rdsChar(blockDHigh);
      radioText_[segment * 2 + 1] = rdsChar(blockDLow);
      radioText_[32] = '\0';
    }
  }
  // Any other group type carries neither PS name nor RadioText -- ignored.
}

void RadioTuner::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  // persistedVolume_, never the live volume_: a mute during a radio-wake
  // ramp (snooze, dead-air fallback) reaches here with volume_ still on
  // whatever quiet transient step the ramp was at, and writing *that*
  // would bring the radio back at e.g. 12 instead of 30 after a reboot.
  prefs.putUChar(kVolumeKey, persistedVolume_);
  prefs.putUChar(kMutedKey, muted_ ? 1 : 0);
  prefs.end();
}

void RadioTuner::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  volume_ = prefs.getUChar(kVolumeKey, RadioConfig::DefaultVolume);
  persistedVolume_ = volume_;
  muted_ = prefs.getUChar(kMutedKey, 0) != 0;
  for (uint8_t i = 0; i < presetCount(); i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%u", i);
    presets_[i] = prefs.getUShort(key, 0);
  }
  prefs.end();
}

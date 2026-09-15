#include "RadioTuner.h"

#include <Preferences.h>
#include <Wire.h>

namespace {
constexpr const char *kNamespace = "radio";
constexpr const char *kVolumeKey = "volume";
constexpr const char *kFreqKey = "freq";

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
  uint16_t freq = prefs.getUShort(kFreqKey, RadioConfig::FmDefaultFreq);
  prefs.end();
  freq = constrain(freq, r.fmBandStart, r.fmBandEnd);
  freq = snapToGrid(freq, r);

  si4735_.setFM(r.fmBandStart, r.fmBandEnd, freq, r.fmStep);
}

void RadioTuner::tune(uint16_t frequency10kHz) {
  rdsFallbackActive_ = false;  // real user action cancels a background sync attempt
  const RegionEntry &r = region_.current();
  frequency10kHz = constrain(frequency10kHz, r.fmBandStart, r.fmBandEnd);
  // Snap onto the region's channel grid -- matters for Americas' 200kHz-
  // spaced odd-decimal grid (88.1, 88.3, ...): a manual dashboard entry or
  // an old preset saved before this existed could otherwise land on an
  // invalid "even" frequency, which would then throw off every subsequent
  // step/seek too, since those just add/subtract fmStep from wherever the
  // radio currently sits -- this is what keeps that self-correcting instead.
  frequency10kHz = snapToGrid(frequency10kHz, r);
  // The preference is persisted either way (matches volume_/muted_ below)
  // so it's already in place for whenever a chip does get connected -- only
  // the actual hardware write is skipped without one.
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUShort(kFreqKey, frequency10kHz);
  prefs.end();
  if (!available_) return;
  si4735_.setFrequency(frequency10kHz);
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

void RadioTuner::seekUp() {
  rdsFallbackActive_ = false;
  if (!available_) return;
  const RegionEntry &r = region_.current();
  uint16_t start = frequency10kHz();
  uint16_t candidate = start;
  // See Config.h's SeekRssiThreshold/SeekSnrThreshold comment for why this
  // steps and settles itself rather than using the chip's own hardware
  // seek. Bounded to one full pass of the band (never less than 1, even if
  // fmStep somehow didn't divide it evenly) so this can't loop forever.
  uint16_t totalSteps = (r.fmBandEnd - r.fmBandStart) / r.fmStep + 1;
  for (uint16_t i = 0; i < totalSteps; i++) {
    uint16_t next = candidate + r.fmStep;
    candidate = (next > r.fmBandEnd) ? r.fmBandStart : next;
    si4735_.setFrequency(candidate);
    delay(RadioConfig::SeekSettleMs);
    if (rssi() >= RadioConfig::SeekRssiThreshold && snr() >= RadioConfig::SeekSnrThreshold) {
      climbToLocalPeak(/*seekingUp=*/true);
      return;
    }
  }
  si4735_.setFrequency(start);  // nothing in the whole band cleared the threshold
}
void RadioTuner::seekDown() {
  rdsFallbackActive_ = false;
  if (!available_) return;
  const RegionEntry &r = region_.current();
  uint16_t start = frequency10kHz();
  uint16_t candidate = start;
  uint16_t totalSteps = (r.fmBandEnd - r.fmBandStart) / r.fmStep + 1;
  for (uint16_t i = 0; i < totalSteps; i++) {
    candidate = (candidate < r.fmBandStart + r.fmStep) ? topOfGrid(r) : candidate - r.fmStep;
    si4735_.setFrequency(candidate);
    delay(RadioConfig::SeekSettleMs);
    if (rssi() >= RadioConfig::SeekRssiThreshold && snr() >= RadioConfig::SeekSnrThreshold) {
      climbToLocalPeak(/*seekingUp=*/false);
      return;
    }
  }
  si4735_.setFrequency(start);
}

void RadioTuner::climbToLocalPeak(bool seekingUp) {
  const RegionEntry &r = region_.current();
  uint16_t bestFreq = frequency10kHz();
  uint8_t bestSnr = snr();
  for (;;) {
    uint16_t next = seekingUp ? bestFreq + r.fmStep : bestFreq - r.fmStep;
    if (next < r.fmBandStart || next > r.fmBandEnd) break;  // band edge -- don't wrap mid-climb
    si4735_.setFrequency(next);
    delay(RadioConfig::SeekSettleMs);
    uint8_t nextSnr = snr();
    if (nextSnr <= bestSnr) break;  // signal is falling off again -- bestFreq was the peak
    bestFreq = next;
    bestSnr = nextSnr;
  }
  si4735_.setFrequency(bestFreq);
}

void RadioTuner::setVolume(uint8_t volume) {
  applyVolume(volume);
  save();
}

void RadioTuner::setVolumeTransient(uint8_t volume) { applyVolume(volume); }

void RadioTuner::applyVolume(uint8_t volume) {
  volume_ = min<uint8_t>(volume, 63);
  updateAmpMutePin();
  if (!available_) return;
  si4735_.setVolume(volume_);
}

void RadioTuner::volumeUp() { setVolume(min<uint8_t>(volume_ + 1, 63)); }
void RadioTuner::volumeDown() { setVolume(volume_ > 0 ? volume_ - 1 : 0); }

void RadioTuner::updateAmpMutePin() {
  digitalWrite(Pins::AmpMute, (muted_ || volume_ == 0) ? HIGH : LOW);
}

void RadioTuner::setMuted(bool muted) {
  if (!muted) rdsFallbackActive_ = false;  // unmuting means the user wants to listen now
  muted_ = muted;
  updateAmpMutePin();
  if (!available_) return;
  si4735_.setAudioMute(muted_);
}

uint16_t RadioTuner::frequency10kHz() {
  if (!available_) return 0;
  return si4735_.getFrequency();
}
uint8_t RadioTuner::rssi() {
  if (!available_) return 0;
  // getCurrentRSSI() just returns a cached field -- nothing populates it
  // without this call first (this is why the Radio screen's "Sig" line
  // always read 0: RadioTuner never made this call at all).
  si4735_.getCurrentReceivedSignalQuality();
  return si4735_.getCurrentRSSI();
}
uint8_t RadioTuner::snr() {
  if (!available_) return 0;
  si4735_.getCurrentReceivedSignalQuality();  // same query populates both RSSI and SNR
  return si4735_.getCurrentSNR();
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
  sleepTimerEndMs_ = millis() + (uint32_t)minutes * 60000UL;
}

void RadioTuner::cancelSleepTimer() { sleepTimerEndMs_ = 0; }

uint16_t RadioTuner::sleepTimerRemainingMinutes() const {
  if (sleepTimerEndMs_ == 0) return 0;
  uint32_t nowMs = millis();
  if (nowMs >= sleepTimerEndMs_) return 0;
  // Ceiling division: exactly N minutes left reads as N, not N+1.
  return (sleepTimerEndMs_ - nowMs + 60000UL - 1) / 60000UL;
}

void RadioTuner::update() {
  if (sleepTimerEndMs_ != 0 && millis() >= sleepTimerEndMs_) {
    setMuted(true);
    sleepTimerEndMs_ = 0;
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
    // longer apply.
    lastRdsFrequency_ = freq;
    psName_[0] = '\0';
    radioText_[0] = '\0';
    radioTextAbFlagKnown_ = false;
  }

  uint8_t raw[13];
  if (!readRdsGroupSafely(raw)) return;  // gave up within bounds -- try again next second
  decodeRdsGroup(raw);
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

  uint8_t groupType = (blockB >> 12) & 0x0F;
  bool isVersionB = (blockB >> 11) & 0x01;

  if (groupType == 0) {
    // Group 0A/0B: PS (station) name. Distribution is identical for both
    // versions -- Block D always carries 2 characters of the 8-char name,
    // for whichever of the 4 segments this group's address selects.
    uint8_t segment = blockB & 0x03;
    psName_[segment * 2] = (char)blockDHigh;
    psName_[segment * 2 + 1] = (char)blockDLow;
    psName_[8] = '\0';
  } else if (groupType == 2) {
    // Group 2A/2B: RadioText. A flip of the text A/B flag means a
    // genuinely new message -- clear the buffer before this segment lands,
    // same as the library's own PS-name handling clears on a change.
    bool abFlag = (blockB >> 4) & 0x01;
    if (!radioTextAbFlagKnown_ || abFlag != radioTextAbFlag_) {
      radioText_[0] = '\0';
      radioTextAbFlag_ = abFlag;
      radioTextAbFlagKnown_ = true;
    }
    uint8_t segment = blockB & 0x0F;
    if (!isVersionB) {
      // 2A: 4 chars/segment (Block C + Block D), 16 segments, 64 chars.
      radioText_[segment * 4] = (char)blockCHigh;
      radioText_[segment * 4 + 1] = (char)blockCLow;
      radioText_[segment * 4 + 2] = (char)blockDHigh;
      radioText_[segment * 4 + 3] = (char)blockDLow;
      radioText_[64] = '\0';
    } else {
      // 2B: 2 chars/segment (Block D only), 16 segments, 32 chars.
      radioText_[segment * 2] = (char)blockDHigh;
      radioText_[segment * 2 + 1] = (char)blockDLow;
      radioText_[32] = '\0';
    }
  }
  // Any other group type carries neither PS name nor RadioText -- ignored.
}

void RadioTuner::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar(kVolumeKey, volume_);
  prefs.end();
}

void RadioTuner::load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  volume_ = prefs.getUChar(kVolumeKey, RadioConfig::DefaultVolume);
  for (uint8_t i = 0; i < presetCount(); i++) {
    char key[8];
    snprintf(key, sizeof(key), "p%u", i);
    presets_[i] = prefs.getUShort(key, 0);
  }
  prefs.end();
}

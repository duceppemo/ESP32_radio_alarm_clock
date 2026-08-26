# Firmware

Built with [PlatformIO](https://platformio.org/) on the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform fork (tracks current Arduino-core/ESP-IDF releases for the ESP32-S3 ahead of the official `espressif32` platform). Target board is the Adafruit ESP32-S3 Reverse TFT Feather (`adafruit_feather_esp32s3_reversetft`).

As of this writing the firmware has not been flashed to real hardware — none of the parts have arrived yet. Everything below is compile-verified only.

## Build & flash

```powershell
pio run                              # build
pio run -t upload                    # flash over the board's default port
pio run -t upload --upload-port COM5 # flash a specific port
pio run -t upload -t monitor         # flash, then open the serial monitor
```

Run these from PowerShell, not Git Bash. Git Bash's MSYS-style environment fails to resolve the `xtensa-esp32s3-elf-g++` toolchain PlatformIO installs, even though the same commands work immediately in PowerShell.

## Testing

Eight modules' logic has real unit tests that run on the host machine — no ESP32 hardware needed:

```powershell
pio test -e native
```

This needs a native C/C++ toolchain (MinGW-w64, via `winget install BrechtSanders.WinLibs.POSIX.UCRT`, or any gcc/clang). `default_envs` in `platformio.ini` keeps a plain `pio run` scoped to the real firmware — `native` only builds when named explicitly, since it has no `main()` outside a test run.

| Suite | Covers |
|---|---|
| `test/test_alarm_clock/` | Day-of-week mask matching, ringing/snoozed state transitions, snooze timing, the dedup-within-a-minute guard, `setAlarm()`/`setSnoozeMinutes()` clamping out-of-range input to a valid hour/minute/snooze-length, persistence across instances. |
| `test/test_radio_tuner/` | FM-band clamping, volume clamping, the transient-vs-persisted volume setter, preset store/recall, sleep timer expiry and its remaining-minutes math, `setSleepTimer(0)` behaving like a cancel rather than an immediately-past deadline, persistence across instances. |
| `test/test_wake_controller/` | The sunrise volume ramp's math, the dead-air RSSI fallback (and that it does *not* fire with a good signal), beep/chime wake sources muting the radio and starting the right tone immediately, what dismissing/snoozing does and doesn't restore. |
| `test/test_snooze_controller/` | The physical snooze button's dual behavior: snoozes a ringing/snoozed alarm (and doesn't also touch the radio's sleep timer while doing so), otherwise toggles the radio's sleep timer on/off if the radio is on, and does nothing if the radio's off. |
| `test/test_menu_system/` | The button-driven screen/state machine, end to end: short/long press, field-by-field alarm editing (including discarding on cancel), Radio-screen tune/mute, the ringing-alarm snooze/dismiss shortcut on Home, the Set Time screen (saves the right hour/minute while preserving the date, cancel discards it, a null `rtc_` doesn't crash, and `setRtcAvailable(false)` skips saving even with a *non-null* `rtc_`), and the Timezone screen (up/down cycles the selection, long-press backs out without losing it). Since `MenuSystem`'s screen/cursor state is private by design, these drive full interaction sequences via simulated button presses and assert on the resulting `AlarmClock`/`RadioTuner`/`RTC_DS3231`/`TimezoneStore` state, the same way a real user's presses would. |
| `test/test_timezone_store/` | The curated timezone list: defaults to UTC, `next()`/`previous()` step by one and wrap at both ends, an out-of-range `setIndex()` falls back to UTC instead of leaving stale state, the selection persists across instances (NVS-backed), and every entry has a non-empty label and POSIX TZ string. |
| `test/test_display_dimmer/` | The lux-to-brightness curve: clamps to minimum at/below the dim threshold and maximum at/above the bright threshold (including out-of-range negative lux), interpolates correctly at the midpoint, and never decreases as lux increases. |
| `test/test_battery_monitor/` | Stays `available() == false` with safe zeroed readings before `begin()` succeeds (and stays that way if `begin()` fails), reports the fuel gauge's real voltage/percent once available, and `isLow()`'s threshold comparison (at/above/below `BatteryConfig::LowPercentThreshold`). |

The `[env:native]` build only compiles the `src/*.cpp` files listed in `build_src_filter` — `WebDashboard` isn't among them and isn't covered: it depends on `ESPAsyncWebServer`/`WiFi`/`ESPmDNS`, and its actual logic is mostly JSON marshaling coupled directly to request/response objects, so faking that stack well enough to test route handlers would be a much bigger, more fragile undertaking for comparatively little payoff versus the other modules here.

`test/native_fakes/` provides minimal, self-contained stand-ins for exactly the hardware API surface each tested module touches — not general-purpose mocks. Pulling in the *real* `RTClib`/`Adafruit BusIO` natively through the `ArduinoFake` mocking library was tried first and abandoned: real Adafruit source code needs things that mock doesn't provide (a `BitOrder` type, a global `min()`), and patching around that would have been a losing game. What's faked instead:

- `RTClib.h` — reimplements only `DateTime`/`TimeSpan`'s calendar math (Howard Hinnant's `days_from_civil`/`civil_from_days` algorithms), verified against Python's `datetime`, plus a minimal `RTC_DS3231` (`adjust()`/`now()`) so `MenuSystem`'s Set Time screen is testable.
- `Preferences.h` — an in-memory map standing in for the ESP32 NVS wrapper. Backed by one process-wide static store, so call `Preferences::resetAll()` in `setUp()` to avoid leaking state between test cases.
- `SI4735.h` — records what `RadioTuner` sets and lets tests control what "the chip" reports back, notably `SI4735::setSimulatedRssi()` for dead-air testing.
- `Adafruit_GFX.h` / `Adafruit_ST7789.h` — no-op display stand-ins, just enough for `MenuSystem` to construct and render without a real screen.
- `Adafruit_MAX1704X.h` — records what `BatteryMonitor` reads and lets tests control what "the chip" reports back (`setSimulatedVoltage()`/`setSimulatedPercent()`/`setSimulatedBeginOk()`), the same pattern as `SI4735.h`'s RSSI simulation.
- `Arduino.h` — a fake `millis()` and per-pin `digitalRead()` that tests control directly (`native_fake_millis_value()`, `native_fake_digital_state(pin)`), plus the handful of other free functions/macros (`pinMode`, `tone`/`noTone`, `min`/`max`, `constrain`) the tested modules call.

## Modules

| File | Responsibility |
|---|---|
| [`src/Config.h`](../src/Config.h) | Pin assignments and shared tunables. All non-bus pins (radio reset, snooze/volume buttons, buzzer) are placeholders pending final wiring — see [`wiring-diagram.html`](wiring-diagram.html). |
| [`src/AlarmClock.*`](../src/AlarmClock.h) | Up to 3 schedules and an idle/ringing/snoozed state machine, each with a `WakeSource` (radio / beep / chime). `setAlarm()`/`setSnoozeMinutes()` clamp hour/minute/snooze-length to valid ranges regardless of caller — the on-device menu only ever produces valid values itself, but the dashboard's JSON API doesn't guarantee that, so the clamp lives here rather than being trusted to every caller. Persisted to NVS via `Preferences`. Only needs a `DateTime` per tick, so it's hardware-independent. |
| [`src/RadioTuner.*`](../src/RadioTuner.h) | Wraps the [PU2CLR SI4735](https://github.com/pu2clr/SI4735) driver for the onboard SI4730 FM tuner: tune/seek/volume/mute, 6 presets, a sleep timer, all persisted to NVS (except the sunrise-ramp's intermediate volume steps, which use a transient setter that skips the flash write). This is control only, over I2C — see the audio-path note below. |
| [`src/AlarmSound.*`](../src/AlarmSound.h) | Drives a piezo buzzer on `Pins::Buzzer` via Arduino's `tone()`/`noTone()`, stepping through a small pattern table: an urgent 1.8/2.2kHz alternating beep, or a gentler ascending A-major-triad chime (A5→C♯6→E6). Used both as a selectable wake sound and as the dead-air fallback below. Entirely unrelated to the radio's audio path. |
| [`src/BatteryMonitor.*`](../src/BatteryMonitor.h) | Wraps the onboard MAX17048 LiPoly fuel gauge (I2C, address 0x36) for voltage/percent/low-battery. |
| [`src/DisplayDimmer.*`](../src/DisplayDimmer.h) | Pure math: maps a VEML7700 lux reading to a TFT backlight PWM duty cycle (0-255, never fully off) and a 7-segment brightness level (0-15, the HT16K33's native range), linearly interpolated between `DisplayConfig::DimLuxThreshold`/`BrightLuxThreshold`. `main.cpp` calls it once a second and applies the result via `analogWrite(TFT_BACKLITE, ...)` / `Adafruit_7segment::setBrightness()`. |
| [`src/TimezoneStore.*`](../src/TimezoneStore.h) | A curated list of timezones (label + POSIX TZ string, DST rule included where applicable), persisted to NVS via `Preferences`. Selectable from the TFT menu's Timezone screen or the dashboard's Time zone dropdown; `WebDashboard` feeds the selected POSIX string into `configTzTime()` on every NTP sync, so it's the single source of truth for local time — nothing is hardcoded. |
| [`src/WakeController.*`](../src/WakeController.h) | Pure orchestration, owns nothing: watches `AlarmClock`'s state and, per the ringing alarm's `WakeSource`, either ramps `RadioTuner`'s volume up over `AlarmConfig::WakeRampSeconds` (falling back to `AlarmSound` if the station turns out to be dead air) or plays the selected tone directly. |
| [`src/SnoozeController.*`](../src/SnoozeController.h) | Pure orchestration, owns nothing: the physical snooze button (`Pins::SnoozeButton`) does different things depending on context — snoozes a ringing/snoozed alarm, or (if idle and the radio is on) toggles `RadioTuner`'s sleep timer on/off at `RadioConfig::DefaultSleepTimerMinutes`. Does nothing if idle and the radio's off/muted. |
| [`src/MenuSystem.*`](../src/MenuSystem.h) | Debounced short/long-press handling for the three onboard buttons (D0/D1/D2) drives a Home / Alarms / Alarm-edit / Radio / WiFi-info / Set-Time / Timezone screen state machine on the built-in TFT. Short press selects/confirms, long press backs out a level; on Home while an alarm is ringing, the same two gestures snooze/dismiss it. Shows battery percent and (when active) the sleep timer's live countdown on Home, and the sleep timer on the Radio screen too. Set Time writes directly to the RTC (`rtc->adjust()`) — the only manual way to correct the clock without WiFi/NTP. Timezone cycles through `TimezoneStore`'s list with up/down, no WiFi needed either. |
| [`src/WebDashboard.*`](../src/WebDashboard.h) + [`src/DashboardPage.h`](../src/DashboardPage.h) | Implements the web dashboard described below. |
| [`src/main.cpp`](../src/main.cpp) | Owns one instance of each module, plus a `DebouncedButton` each for the snooze and two volume buttons. `MenuSystem::update()`, `WebDashboard::update()`, `WakeController::tickFast()` (keeps a playing tone fed), and all three panel buttons run every `loop()` iteration for responsiveness; RTC reads, alarm evaluation, `RadioTuner::update()` (sleep timer expiry), and `WakeController::tickSlow()` are throttled to 1 Hz to limit I2C traffic. |

## Web dashboard

On boot, `WebDashboard` loads WiFi credentials from NVS. If none are stored (or the stored network can't be reached within 15s), it starts an access point (`AlarmClock-Setup`); otherwise it joins the home network, starts mDNS at `alarmclock.local`, and syncs the RTC from NTP (re-synced daily, plus immediately on every timezone change) using the NTP server from `Config.h` and the POSIX TZ string from the currently selected `TimezoneStore` entry — the timezone itself is a user setting, chosen from a curated list on the TFT's Timezone screen or the dashboard's Time zone dropdown, not hardcoded. Either way it serves the same single-page dashboard (`DashboardPage.h`) over a small JSON API, plus an OTA firmware-update page at `/update` (via `ElegantOTA`, which the board's partition table supports — it has proper `ota_0`/`ota_1` slots):

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | The dashboard page |
| `/update` | GET | ElegantOTA firmware upload page |
| `/api/status` | GET | Mode (AP/STA), IP, RTC time, alarm state, battery percent/voltage/low-flag; while in AP mode, also the generated dashboard login (see Login below) |
| `/api/alarms` | GET / POST | List / update the 3 alarm schedules, including `wakeSource` |
| `/api/alarm/snooze`, `/api/alarm/dismiss` | POST | Control a ringing alarm |
| `/api/radio` | GET / POST | Read status (incl. sleep timer remaining); tune, seek, volume/mute, recall/store a preset, set/cancel the sleep timer |
| `/api/wifi` | POST | Save `{ssid, password}` and reboot to join that network |
| `/api/timezone` | GET / POST | Read the selected timezone + full option list; change it by index (re-syncs NTP immediately if online) |
| `/api/security` | GET / POST | Read the current login username; change username/password |
| `/api/settings` | GET / POST | Export/import alarms + snooze duration + radio volume/presets + timezone as one JSON blob (excludes WiFi credentials and the dashboard login) |

Built on `esp32async/ESPAsyncWebServer` + `esp32async/AsyncTCP` (the actively maintained fork — not the archived `me-no-dev` originals), `bblanchon/ArduinoJson` v7, and `ayushsharma82/ElegantOTA`.

### Login

Every route requires HTTP Basic Auth against a username/password stored in NVS — *except* while the device is still in AP setup mode, where the open `AlarmClock-Setup` network is itself the trust boundary (you already had to be physically close enough to join it), so the dashboard is reachable with no login there. `/update` (firmware flashing) is the one exception that's always gated, in both modes, since it's the highest-risk action.

On first boot `WebDashboard::loadOrCreateAdminCredentials()` generates a random 32-bit-entropy default password via the ESP32's hardware RNG (`esp_random()` — deliberately *not* derived from the chip's MAC, since that's visible to anyone sniffing the setup AP's beacon frames) and persists it. That password is shown right on the unauthenticated AP-mode dashboard page so it can be copied down before joining the home network, where auth starts being enforced. Change it anytime from the dashboard's Security section, which also re-applies it to `/update`'s auth immediately.

## Two independent audio paths

Easy to mix up, so worth stating plainly:

- **FM/AM playback**: `SI4730 (analog audio out) → amp → speaker`. Pure analog, wired straight to each other. The ESP32 is never in this signal path — it only talks to the SI4730 over I2C to send control commands (tune, volume, seek), the same bus as the RTC and light sensor.
- **Alarm tones**: `ESP32 → piezo buzzer` on `Pins::Buzzer`, via `AlarmSound`. Unrelated to the amp/speaker chain entirely.

So there are two independent sound sources on this device, not one shared path: the buzzer for a simple alarm tone, the amp+speaker for actual radio audio.

## Wake sources and the dead-air fallback

Each alarm picks a `WakeSource`: **Radio** ramps `RadioTuner`'s volume from `AlarmConfig::WakeRampStartVolume` up to whatever was last set, over `WakeRampSeconds` (90s by default) — this is still just an I2C volume command to the SI4730, controlling its actual analog output level. A few seconds in, `WakeController` checks `RadioTuner::rssi()` against `AlarmConfig::DeadAirRssiThreshold` and, if the tuned station is silent, mutes the radio (also over I2C, which cuts its analog output) and switches to the buzzer's beep tone instead. **Beep** and **Chime** skip the radio entirely and play that tone from the start.

## Known gaps

- The `Config.h` pin assignments (including the buzzer) and the SI4735 reset-pin timing are unverified against real wiring.
- The dead-air RSSI threshold (`AlarmConfig::DeadAirRssiThreshold`) is a guess and will need retuning once there's a real signal to measure.
- The buzzer tone frequencies/pattern (`AlarmSound.cpp`) are a best guess at what sounds good on a typical passive piezo — worth listening to and retuning once hardware exists.
- `DisplayDimmer`'s lux thresholds (`DisplayConfig::DimLuxThreshold`/`BrightLuxThreshold`) and brightness floors are guesses, same as the RSSI threshold above — untested against a real room or a real TFT/7-segment's actual visible range.

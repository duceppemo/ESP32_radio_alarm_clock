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

## Modules

| File | Responsibility |
|---|---|
| [`src/Config.h`](../src/Config.h) | Pin assignments and shared tunables. All non-bus pins (radio reset, snooze button, encoder, buzzer) are placeholders pending final wiring — see [`wiring-diagram.html`](wiring-diagram.html). |
| [`src/AlarmClock.*`](../src/AlarmClock.h) | Up to 3 schedules and an idle/ringing/snoozed state machine, each with a `WakeSource` (radio / beep / chime). Persisted to NVS via `Preferences`. Only needs a `DateTime` per tick, so it's hardware-independent. |
| [`src/RadioTuner.*`](../src/RadioTuner.h) | Wraps the [PU2CLR SI4735](https://github.com/pu2clr/SI4735) driver for the onboard SI4730 FM tuner: tune/seek/volume/mute, 6 presets, a sleep timer, all persisted to NVS (except the sunrise-ramp's intermediate volume steps, which use a transient setter that skips the flash write). This is control only, over I2C — see the audio-path note below. |
| [`src/AlarmSound.*`](../src/AlarmSound.h) | Drives a piezo buzzer on `Pins::Buzzer` via Arduino's `tone()`/`noTone()`, stepping through a small pattern table: an urgent 1.8/2.2kHz alternating beep, or a gentler ascending A-major-triad chime (A5→C♯6→E6). Used both as a selectable wake sound and as the dead-air fallback below. Entirely unrelated to the radio's audio path. |
| [`src/BatteryMonitor.*`](../src/BatteryMonitor.h) | Wraps the onboard MAX17048 LiPoly fuel gauge (I2C, address 0x36) for voltage/percent/low-battery. |
| [`src/WakeController.*`](../src/WakeController.h) | Pure orchestration, owns nothing: watches `AlarmClock`'s state and, per the ringing alarm's `WakeSource`, either ramps `RadioTuner`'s volume up over `AlarmConfig::WakeRampSeconds` (falling back to `AlarmSound` if the station turns out to be dead air) or plays the selected tone directly. |
| [`src/MenuSystem.*`](../src/MenuSystem.h) | Debounced short/long-press handling for the three onboard buttons (D0/D1/D2) drives a Home / Alarms / Alarm-edit / Radio / WiFi-info screen state machine on the built-in TFT. Short press selects/confirms, long press backs out a level; on Home while an alarm is ringing, the same two gestures snooze/dismiss it. Shows battery percent on Home and the sleep timer on the Radio screen when available. |
| [`src/WebDashboard.*`](../src/WebDashboard.h) + [`src/DashboardPage.h`](../src/DashboardPage.h) | Implements the web dashboard described below. |
| [`src/main.cpp`](../src/main.cpp) | Owns one instance of each module. `MenuSystem::update()`, `WebDashboard::update()`, and `WakeController::tickFast()` (keeps a playing tone fed) run every `loop()` iteration; RTC reads, alarm evaluation, `RadioTuner::update()` (sleep timer expiry), and `WakeController::tickSlow()` are throttled to 1 Hz to limit I2C traffic. |

## Web dashboard

On boot, `WebDashboard` loads WiFi credentials from NVS. If none are stored (or the stored network can't be reached within 15s), it starts an access point (`AlarmClock-Setup`); otherwise it joins the home network, starts mDNS at `alarmclock.local`, and syncs the RTC from NTP (re-synced daily; server and POSIX timezone string are in `Config.h` — adjust `NetConfig::PosixTimezone` to your locale). Either way it serves the same single-page dashboard (`DashboardPage.h`) over a small JSON API, plus an OTA firmware-update page at `/update` (via `ElegantOTA`, which the board's partition table supports — it has proper `ota_0`/`ota_1` slots):

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | The dashboard page |
| `/update` | GET | ElegantOTA firmware upload page |
| `/api/status` | GET | Mode (AP/STA), IP, RTC time, alarm state, battery percent/voltage/low-flag |
| `/api/alarms` | GET / POST | List / update the 3 alarm schedules, including `wakeSource` |
| `/api/alarm/snooze`, `/api/alarm/dismiss` | POST | Control a ringing alarm |
| `/api/radio` | GET / POST | Read status (incl. sleep timer remaining); tune, seek, volume/mute, recall/store a preset, set/cancel the sleep timer |
| `/api/wifi` | POST | Save `{ssid, password}` and reboot to join that network |
| `/api/settings` | GET / POST | Export/import alarms + snooze duration + radio volume/presets as one JSON blob (excludes WiFi credentials) |

Built on `esp32async/ESPAsyncWebServer` + `esp32async/AsyncTCP` (the actively maintained fork — not the archived `me-no-dev` originals), `bblanchon/ArduinoJson` v7, and `ayushsharma82/ElegantOTA`.

## Two independent audio paths

Easy to mix up, so worth stating plainly:

- **FM/AM playback**: `SI4730 (analog audio out) → amp → speaker`. Pure analog, wired straight to each other. The ESP32 is never in this signal path — it only talks to the SI4730 over I2C to send control commands (tune, volume, seek), the same bus as the RTC and light sensor.
- **Alarm tones**: `ESP32 → piezo buzzer` on `Pins::Buzzer`, via `AlarmSound`. Unrelated to the amp/speaker chain entirely.

So there are two independent sound sources on this device, not one shared path: the buzzer for a simple alarm tone, the amp+speaker for actual radio audio.

## Wake sources and the dead-air fallback

Each alarm picks a `WakeSource`: **Radio** ramps `RadioTuner`'s volume from `AlarmConfig::WakeRampStartVolume` up to whatever was last set, over `WakeRampSeconds` (90s by default) — this is still just an I2C volume command to the SI4730, controlling its actual analog output level. A few seconds in, `WakeController` checks `RadioTuner::rssi()` against `AlarmConfig::DeadAirRssiThreshold` and, if the tuned station is silent, mutes the radio (also over I2C, which cuts its analog output) and switches to the buzzer's beep tone instead. **Beep** and **Chime** skip the radio entirely and play that tone from the start.

## Known gaps

- Physical snooze button, rotary encoder volume control, and VEML7700-driven display auto-dimming have pins reserved in `Config.h` but no polling/handling code yet.
- The `Config.h` pin assignments (including the buzzer) and the SI4735 reset-pin timing are unverified against real wiring. The piezo buzzer isn't yet in the hardware BOM.
- The dead-air RSSI threshold (`AlarmConfig::DeadAirRssiThreshold`) is a guess and will need retuning once there's a real signal to measure.
- The buzzer tone frequencies/pattern (`AlarmSound.cpp`) are a best guess at what sounds good on a typical passive piezo — worth listening to and retuning once hardware exists.

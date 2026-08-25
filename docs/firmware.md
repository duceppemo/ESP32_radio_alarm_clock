# Firmware

Built with [PlatformIO](https://platformio.org/) on the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform fork (tracks current Arduino-core/ESP-IDF releases for the ESP32-S3 ahead of the official `espressif32` platform). Target board is the Adafruit ESP32-S3 Reverse TFT Feather (`adafruit_feather_esp32s3_reversetft`).

As of this writing the firmware has not been flashed to real hardware — none of the parts have arrived yet. Everything below is compile-verified only.

## Build & flash

```
pio run                    # build
pio run -t upload          # flash
pio run -t upload -t monitor
```

On Windows, run PlatformIO commands from **PowerShell**, not Git Bash. Git Bash's MSYS-style environment fails to resolve the `xtensa-esp32s3-elf-g++` toolchain PlatformIO installs, even though the same commands work immediately in PowerShell.

## Modules

| File | Responsibility |
|---|---|
| [`src/Config.h`](../src/Config.h) | Pin assignments and shared tunables. All non-bus pins (radio reset, snooze button, encoder, I2S amp) are placeholders pending final wiring — see [`wiring-diagram.html`](wiring-diagram.html). |
| [`src/AlarmClock.*`](../src/AlarmClock.h) | Up to 3 schedules and an idle/ringing/snoozed state machine. Persisted to NVS via `Preferences`. Only needs a `DateTime` per tick, so it's hardware-independent. |
| [`src/RadioTuner.*`](../src/RadioTuner.h) | Wraps the [PU2CLR SI4735](https://github.com/pu2clr/SI4735) driver for the onboard SI4730 FM tuner: tune/seek/volume/mute, 6 presets, all persisted to NVS. |
| [`src/MenuSystem.*`](../src/MenuSystem.h) | Debounced short/long-press handling for the three onboard buttons (D0/D1/D2) drives a Home / Alarms / Alarm-edit / Radio / WiFi-info screen state machine on the built-in TFT. Short press selects/confirms, long press backs out a level; on Home while an alarm is ringing, the same two gestures snooze/dismiss it. |
| [`src/WebDashboard.*`](../src/WebDashboard.h) + [`src/DashboardPage.h`](../src/DashboardPage.h) | Implements the web dashboard described below. |
| [`src/main.cpp`](../src/main.cpp) | Owns one instance of each module. `MenuSystem::update()` and `WebDashboard::update()` run every `loop()` iteration for responsiveness; RTC reads, alarm evaluation, and the 7-segment refresh are throttled to 1 Hz to limit I2C traffic. |

## Web dashboard

On boot, `WebDashboard` loads WiFi credentials from NVS. If none are stored (or the stored network can't be reached within 15s), it starts an access point (`AlarmClock-Setup`); otherwise it joins the home network and starts mDNS at `alarmclock.local`. Either way it serves the same single-page dashboard (`DashboardPage.h`) over a small JSON API:

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | The dashboard page |
| `/api/status` | GET | Mode (AP/STA), IP, RTC time, alarm state |
| `/api/alarms` | GET / POST | List / update the 3 alarm schedules |
| `/api/alarm/snooze`, `/api/alarm/dismiss` | POST | Control a ringing alarm |
| `/api/radio` | GET / POST | Read status; tune, seek, set volume/mute, recall/store a preset |
| `/api/wifi` | POST | Save `{ssid, password}` and reboot to join that network |

Built on `esp32async/ESPAsyncWebServer` + `esp32async/AsyncTCP` (the actively maintained fork — not the archived `me-no-dev` originals) and `bblanchon/ArduinoJson` v7.

## Known gaps

- Physical snooze button, rotary encoder volume control, and VEML7700-driven display auto-dimming have pins reserved in `Config.h` but no polling/handling code yet.
- The `Config.h` pin assignments and the SI4735 reset-pin timing are unverified against real wiring.

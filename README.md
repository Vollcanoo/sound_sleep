# SleepMonitor ESP32-S3 Firmware

This project is the ESP32-S3 firmware for a pillow-based sleep monitor. It
combines three FSR pressure sensors, an INMP441 microphone, BLE Wi-Fi
provisioning, a local ESP-DL snore model, sleep-session aggregation, cloud LLM
analysis, and two airbag outputs.

The firmware target is **ESP32-S3 with 8 MB flash and PSRAM**. It is built with
ESP-IDF 5.5.2 on Windows.

## Current Status

Implemented and integrated:

- Three FSR sensors classify `SUPINE`, `LEFT_SIDE`, `RIGHT_SIDE`, `MOVING`, and
  `NO_HEAD`.
- INMP441 audio is classified locally by the embedded ESP-DL model.
- BLE Nordic UART Service exposes Wi-Fi provisioning, calibration, and
  monitoring control.
- The mobile app sends `monitor_start` and `monitor_stop`.
- Snore capture/inference, feature aggregation, airbag rules, and cloud sleep
  sessions are disabled until `monitor_start` is received.
- BLE remains discoverable after Wi-Fi connects and after an app-side unbind.

Not yet complete or not yet hardware-verified is listed in
[Known Limitations](#known-limitations). Do not treat this as a medical device
or use its output for diagnosis.

## Hardware Wiring

### ESP32-S3 pin map

| Function | ESP32-S3 pin | Notes |
| --- | --- | --- |
| Left FSR ADC | GPIO4 | ADC input |
| Center FSR ADC | GPIO5 | ADC input |
| Right FSR ADC | GPIO6 | ADC input |
| INMP441 BCLK/SCK | GPIO16 | I2S clock |
| INMP441 WS/LRCLK | GPIO15 | I2S word-select |
| INMP441 SD/DOUT | GPIO17 | I2S microphone data output |
| Left airbag pump | GPIO7 | Drive through MOSFET circuit, never directly |
| Right airbag pump | GPIO8 | Drive through MOSFET circuit, never directly |
| Left airbag valve | GPIO9 | Drive through MOSFET circuit |
| Right airbag valve | GPIO10 | Drive through MOSFET circuit |

### Each FSR circuit

Each FSR has two terminals and is not polarity-sensitive. Build **three
independent voltage dividers**; do not share the ADC junctions.

```text
3V3 ---- FSR ----+---- GPIO4 / GPIO5 / GPIO6
                 |
               2 kOhm
                 |
                GND
```

- Left sensor uses GPIO4, center uses GPIO5, right uses GPIO6.
- The 2 kOhm resistor is the fixed resistor confirmed for this project.
- All three sensor grounds and ESP32 GND must be common.
- At boot, keep the pillow unloaded for the initial FSR baseline calibration.
- If an idle ADC channel stays at `4095`, the ADC point is probably pulled up
  or open. If it stays at `0`, check the GND path and the resistor connection.

### INMP441 microphone

| INMP441 pin | Connect to |
| --- | --- |
| VDD | 3V3 |
| GND | ESP32 GND |
| SCK/BCLK | GPIO16 |
| WS/LRCL | GPIO15 |
| SD | GPIO17 |
| L/R | 3V3 for the configured right channel |

The firmware uses the right I2S slot by default
(`CONFIG_SNORE_I2S_USE_RIGHT_CHANNEL=y`). If the microphone produces silence,
first check the common ground and L/R level. Do not connect INMP441 VDD to 5 V.

### Pumps and valves

GPIO7-GPIO10 are logic-control pins only. Pumps and solenoid valves need their
own suitable power supply, flyback protection where applicable, and MOSFET
driver circuits. The ESP32 ground must be common with the driver ground.

## Software Prerequisites

### Firmware host

- Windows 10/11
- ESP-IDF `v5.5.2` with the ESP32-S3 toolchain
- Git
- A USB data cable for the board

Open an **ESP-IDF PowerShell** before running `idf.py`. In an ordinary
PowerShell, `idf.py` may not exist or the Xtensa compiler may be absent from
`PATH`.

### Local ESP-DL dependencies

The model dependencies are intentionally ignored by Git because they are large.
A fresh checkout must contain these folders before configuring CMake:

```text
Snore_Det/third_party/esp-dl/esp-dl/CMakeLists.txt
Snore_Det/third_party/esp-dsp/CMakeLists.txt
Snore_Det/third_party/esp_new_jpeg/CMakeLists.txt
```

One way to obtain them is:

```powershell
cd E:\sound_sleep
New-Item -ItemType Directory -Force .\Snore_Det\third_party | Out-Null
git clone --depth 1 --branch v3.3.8 https://github.com/espressif/esp-dl.git .\Snore_Det\third_party\esp-dl
git clone --depth 1 --branch v1.7.0 https://github.com/espressif/esp-dsp.git .\Snore_Det\third_party\esp-dsp
git clone --depth 1 --branch v1.0.2 https://github.com/espressif/esp_new_jpeg.git .\Snore_Det\third_party\esp_new_jpeg
```

If the component registry is unreachable, the project uses these local paths
instead. See [Snore_Det/third_party/README.md](Snore_Det/third_party/README.md)
for the expected folder layout.

### Model and cloud secret

The ESP-DL model is not committed. Place the supplied file here:

```text
main/models/snoring_esp32_int8.espdl
```

Create a local secret header from the template and put the API key **inside
quotes**:

```powershell
Copy-Item .\main\secrets.h.example .\main\secrets.h
```

```c
#define VOLCENGINE_API_KEY "your-api-key"
```

`main/secrets.h` and the model file are ignored by Git. Never commit an API
key, and revoke any key that has been pasted into a chat or public log.

## Build and Flash

The checked-in defaults select `esp32s3`, 8 MB flash, PSRAM, and the custom
partition table. The app partition is 3 MB, which is required by the snore
model firmware.

```powershell
cd E:\sound_sleep
idf.py set-target esp32s3
idf.py build
idf.py -p COM7 flash monitor
```

Replace `COM7` with the board port reported by Windows Device Manager or:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

`flash monitor` automatically builds before flashing, so running `idf.py build`
first is optional. Exit the serial monitor with `Ctrl+]` before attempting a
second flash.

### When the build directory is invalid

If ESP-IDF says `build doesn't seem to be a CMake build directory`, remove only
the generated directory and configure again:

```powershell
Remove-Item -LiteralPath .\build -Recurse -Force
idf.py set-target esp32s3
idf.py build
```

Do not use `usbipd attach --wsl` for this Windows workflow: it transfers the
USB device to WSL and makes the Windows COM port disappear.

## Airbag Bench Test

The firmware includes a minimal, compile-time selectable bench-test mode for
the left airbag outputs. It starts neither Wi-Fi nor BLE, sensors, snore
inference, cloud clients, nor sleep-control tasks. Use it only while directly
observing the GPIO signals and actuator hardware.

Use the dedicated defaults file, SDK config, and build directory below. This avoids the
optional snore-model dependencies, which are intentionally not included in a
fresh checkout. This setting changes the compiled firmware, so use a separate
build directory from the regular sleep-monitor firmware.

The ESP-IDF 5.5.5 GCC/ccache toolchain can fail with `filesystem error:
Cannot convert character sequence` when the project path contains Chinese
characters. Build this firmware from an actual ASCII-only project copy.
`subst` is not sufficient because CMake resolves it back to the original path.

```powershell
$source = 'E:\物联网\sound_sleep'
$bench = 'E:\sound_sleep_bench_esp32s3'
robocopy $source $bench /E /XD .git build build-bench build-bench-utf8 build-bench-esp32s3
if ($LASTEXITCODE -gt 7) { throw "robocopy failed: $LASTEXITCODE" }
Set-Location $bench

$env:PYTHONUTF8 = '1'
$env:IDF_COMPONENT_MANAGER = '0'
$env:IDF_TARGET = 'esp32s3'

idf.py -B build-bench-esp32s3 `
  -D IDF_TARGET=esp32s3 `
  -D SDKCONFIG=sdkconfig.bench `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.bench.defaults" reconfigure

idf.py -B build-bench-esp32s3 `
  -D IDF_TARGET=esp32s3 `
  -D SDKCONFIG=sdkconfig.bench `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.bench.defaults" build

idf.py -B build-bench-esp32s3 `
  -D IDF_TARGET=esp32s3 `
  -D SDKCONFIG=sdkconfig.bench `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.bench.defaults" `
  -p COM7 flash monitor
```

The separate `sdkconfig.bench` is important: an existing regular `sdkconfig`
can explicitly disable `PUMP_BENCH_TEST`, which would otherwise make CMake
build the full firmware instead of the minimal bench firmware. Use a new or
empty ASCII-only `$bench` directory to avoid reusing a stale CMake cache.

`IDF_COMPONENT_MANAGER=0` is safe for this bench firmware because it only
uses built-in ESP-IDF components. It prevents the test build from resolving
or rewriting the normal firmware's optional ESP-DL dependency lock file.

Send these commands through the serial monitor:

| Command | Duration | GPIO7 left pump | GPIO9 left valve |
| --- | --- | --- | --- |
| `left_inflate 2` | 1-5 seconds | LOW | LOW |
| `left_deflate 2` | 1-5 seconds | HIGH | HIGH |
| `left_idle` | immediate | HIGH | LOW |
| `help` | - | - | - |

The test firmware reads these commands through the board's USB Serial/JTAG
port, so use the same COM port opened by `idf.py monitor`; no separate UART
adapter is required.

The default duration is two seconds when omitted. Every completed command
restores the idle state (`GPIO7=HIGH`, `GPIO9=LOW`). The test firmware limits
each command to five seconds and always closes the left valve before starting
the left pump.

## Mobile App Setup

The Flutter app is maintained in the separate worktree/repository checkout
used by this project. It requires Flutter stable with Dart 3.12.2 or newer,
Android SDK, a JDK, and a USB-debuggable Android phone.

```powershell
cd E:\sound_sleep_snore_publish
flutter pub get
flutter build apk --debug

$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r .\build\app\outputs\flutter-apk\app-debug.apk
```

The Android app must be granted Bluetooth/nearby-device permission. When a
phone is connected by USB, verify it with `adb devices`. If `flutter run` does
not recognise an otherwise valid phone, installing the generated APK with ADB
is an acceptable workaround.

## First-Time Use

1. Wire the sensors and microphone, then power the board with the pillow
   unloaded so FSR baseline calibration completes.
2. Flash the firmware. The serial log should show BLE advertising as
   `SleepMonitor`.
3. In the app, open **My Devices**, scan, select `SleepMonitor`, and connect.
   The app filters by the device name and Nordic UART Service UUID.
4. Send Wi-Fi credentials from the app. ESP32 supports **2.4 GHz Wi-Fi only**.
   A disconnect `reason=201` means the access point was not found, commonly
   because a phone hotspot is 5 GHz-only, hidden, out of range, or using an
   unsupported channel.
   The board retains credentials in NVS but does **not** auto-connect on boot;
   send the Wi-Fi configuration command again after a reboot to connect.
5. After Wi-Fi connects, BLE deliberately remains enabled. This is needed for
   the monitor controls and to allow future rebinding.
6. On the dashboard, tap **Start Monitoring**. The app writes `monitor_start`.
   Only then does the board capture microphone windows, run snore inference,
   aggregate features, run airbag rules, and process sleep sessions.
7. Tap **Stop Monitoring** to write `monitor_stop` and clear the board-side
   session. Sliding a device left to unbind also sends `monitor_stop` before
   disconnecting.
8. After unbinding, scan again. The board should return to advertising
   `SleepMonitor`; unbinding removes only the phone-side association and does
   not erase board Wi-Fi credentials.

Useful serial log checkpoints:

```text
BLE_UART: BLE broadcast started: "SleepMonitor"
BLE_UART: Monitoring enabled by BLE
MAIN: Monitoring started by BLE command
SNORE: probability=...
BLE_UART: Monitoring disabled by BLE
BLE_UART: client disconnected
BLE_UART: BLE broadcast started: "SleepMonitor"
```

Before Start Monitoring, `SNORE: probability=...` should not appear. FSR
baseline/pressure log lines may still appear because the pressure task remains
alive for calibration and readiness.

## BLE Protocol

The board implements Nordic UART Service (NUS).

| Item | UUID |
| --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| ESP32 notify TX | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| Phone write RX | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |

Phone-to-board commands:

| Payload | Result |
| --- | --- |
| `monitor_start` | Enables snore capture, aggregation, cloud/session processing, and pump rules. |
| `monitor_stop` | Disables monitoring and discards the in-progress board-side session. |
| `b` | Requests a new unloaded FSR baseline calibration. |
| `{"cmd":"wifi_config","ssid":"...","pass":"..."}` | Saves credentials in NVS and starts Wi-Fi connection. |

The board notifies `monitor_started`, `monitor_stopped`, Wi-Fi provisioning
status, and other status messages through TX.

## Data and Session Rules

- FSR sampling is 10 Hz with a 31-sample median window; posture is published
  internally at 1 Hz.
- `total_pressure >= 50` is treated as head/pillow present.
- A pressure absence of 15 seconds records one get-up event.
- A pressure absence of 5 minutes ends a session.
- Sessions under 5 minutes are ignored for cloud/LLM analysis to prevent test
  sessions from generating reports.
- Snore detection uses a local softmax probability threshold of `0.5`.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `main/main.c` | Startup, task orchestration, session and cloud flow |
| `main/posture_sensor.c` | FSR ADC acquisition, baseline, and posture classification |
| `main/snore_detector.cpp` | ESP-DL snore model task |
| `main/monitor_control.*` | BLE-controlled monitoring gate |
| `main/ble_uart_server.c` | NUS BLE server, provisioning, and monitoring commands |
| `main/wifi_manager.c` | Wi-Fi STA connection and reconnect handling |
| `main/sleep_session.c` | Session summary and end detection |
| `main/cloud_llm_client.c` | Volcengine LLM request |
| `main/cloud_upload.c` | CloudBase upload client |
| `Snore_Det/` | Snore model integration and local third-party dependencies |

## Known Limitations

These are intentionally documented rather than hidden. They are not resolved
by this commit.

1. **BLE realtime posture CSV is not currently emitted by firmware.** The app
   subscribes to NUS TX and can parse the intended 14-column CSV contract, but
   the current firmware does not call `ble_uart_send()` for posture/feature
   frames. Therefore, monitor start/stop control works, but the app's realtime
   posture and pressure display must not yet be treated as live board data.
2. **The new start/stop and rebind behaviour still needs a full hardware
   regression test after flashing this exact firmware.** Expected logs are
   listed above; record any deviation before changing unrelated code.
3. **Wi-Fi is 2.4 GHz only.** Phone hotspots often default to 5 GHz and cause
   `WIFI_REASON_NO_AP_FOUND` (`reason=201`). Use a visible 2.4 GHz hotspot or
   router SSID.
4. **I2S compilation emits ESP-IDF legacy-API deprecation warnings.** They do
   not block the current build, but `audio_capture.cpp` should migrate to
   `driver/i2s_std.h` in a later maintenance task.
5. **FSR thresholds are hardware-dependent.** Baseline calibration and the
   `total_pressure` threshold may need retuning after changing sensor placement,
   foam thickness, resistor values, or supply wiring.
6. **OSA risk output is not a clinical assessment.** Snore probability and
   pressure posture are insufficient to diagnose OSA. Any cloud LLM output is
   advisory only and must be labelled accordingly.

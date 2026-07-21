# SleepMonitor Flutter App

Android companion app for the ESP32-S3 SleepMonitor firmware. The app scans for
the `SleepMonitor` BLE peripheral, performs BLE Wi-Fi provisioning, and sends
the monitor start/stop commands.

The matching firmware documentation, wiring diagram, and ESP-IDF build steps
are in the firmware checkout at `E:\sound_sleep\README.md`.

## Prerequisites

- Flutter stable with Dart 3.12.2 or newer
- Android SDK and accepted Android SDK licenses
- JDK configured for Gradle
- Android phone with Developer options and USB debugging enabled
- Bluetooth and Nearby devices permission granted to the app

Verify the toolchain and phone:

```powershell
flutter doctor
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb devices
```

The phone must appear with state `device`, not `unauthorized` or `offline`.

## Build and Install

```powershell
cd E:\sound_sleep_snore_publish
flutter pub get
flutter build apk --debug

$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb install -r .\build\app\outputs\flutter-apk\app-debug.apk
```

The debug APK is produced at:

```text
build/app/outputs/flutter-apk/app-debug.apk
```

If Gradle cannot download dependencies, configure the HTTP/HTTPS proxy used by
your local network before running the build. If `flutter run` marks a connected
phone as unsupported while `adb devices` reports `device`, use the APK install
command above instead.

## Use With the Board

1. Flash and power the ESP32-S3 firmware. Confirm it advertises as
   `SleepMonitor` in the serial log.
2. Open **My Devices**, scan, select `SleepMonitor`, and bind it. Scanning
   filters by the board name or Nordic UART Service UUID, avoiding unrelated
   nearby devices.
3. Send the Wi-Fi SSID and password. The ESP32 supports 2.4 GHz Wi-Fi only.
4. Return to the dashboard and tap **Start Monitoring**. The app sends
   `monitor_start` using a BLE write with response. It only displays the
   monitoring state after the write succeeds.
5. Tap **Stop Monitoring** to send `monitor_stop`.
6. Swipe a bound device left to unbind. The app sends `monitor_stop`, waits for
   the write, then disconnects. The board keeps BLE advertising, so it can be
   scanned and bound again later.

## BLE Contract

The app uses Nordic UART Service:

| Item | UUID |
| --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Board notify TX | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| Phone write RX | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |

Commands sent by this app:

- `monitor_start`
- `monitor_stop`
- `{"cmd":"wifi_config","ssid":"...","pass":"..."}`

## Current Limitations

1. The app can parse the planned posture CSV stream, but the present firmware
   does not emit posture frames through NUS TX. The realtime posture/pressure
   widgets must therefore not be interpreted as verified live board data yet.
2. BLE monitor-control behaviour requires a hardware regression after every
   firmware flash: no snore inference before Start, inference after Start, and
   `SleepMonitor` advertising again after unbind.
3. The app is currently developed and tested for Android. Desktop/mock paths
   are development aids, not hardware validation.
4. Sleep/OSA-related values are not medical advice or diagnostic output.

## Sensitive Files

Do not place cloud API keys in the mobile app. Keep any optional local secret
file out of version control. Firmware secrets belong only in the firmware
checkout's ignored `main/secrets.h`.

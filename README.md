# Arduino-TimeServices

A header-only Arduino library that gathers the time from NTP servers, a GNSS receiver and Bluetooth LE peers, combines it into one clock estimate and serves that time to other devices over NTP and the Bluetooth LE Current Time Service. A touch GUI shows the clock, the sources, the servers and charts.

This is the Arduino port of [Android-Time-Services](https://github.com/pschatzmann/Android-Time-Services). It uses [TinyNTP](https://github.com/pschatzmann/TinyNTP) for NTP, [TinyCTS](https://github.com/pschatzmann/TinyCTS) for the Bluetooth Current Time Service, and [TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign) for the GUI.

## Features

- **Sources:** a configurable list of NTP servers (TinyNTP), a GNSS receiver (NMEA over serial, optional PPS pin) and BLE Current Time Service peers (TinyCTS).
- **Fusion:** weighted mean ("Kalman"), median ("Marzullo") or PLL/FLL smoothing, outlier rejection, holdover, reach, jitter, drift, root delay and dispersion.
- **Publishing:** an NTP server (TinyNTPServer with allowed subnets and a rate limit) and a BLE Current Time Service peripheral (TinyCTSServer). You choose what is published: the fused clock, the system clock or a single source.
- **System clock:** optionally sets the clock of the device (settimeofday()) to the fused time.
- **GUI:** 5 tabs:
  - Clock: local, UTC, TAI or GPS time; the time zone with DST and its CTS encoding; the synchronisation quality.
  - Sources
  - Publish
  - Metrics
  - Settings
- **Layered:** use only the engine and the sources and servers (no display), or the complete touch GUI.
- **Desktop build** with CMake: the GUI in an SDL2 window with real NTP queries, plus the unit tests.

## Architecture

| Layer | Classes |
|---|---|
| Engine | `TimeEngine` (fusion, telemetry, alerts, `published()`), `LocalClock` / `SystemClock`, `TimeZoneInfo` (TinyTimeZone, any IANA zone) |
| Sources | `NtpSources<UDP>` (TinyNTPClient), `GnssSource` (TinyGPSTimeSource: NMEA + PPS), `CtsSource` (TinyCTSClient, `TimeServices/BleCts.h`) |
| Publishers | `NtpPublisher<UDP>` (TinyNTPServer), `CtsPublisher` (TinyCTSServer, `TimeServices/BleCts.h`) |
| GUI | `TimeServicesApp` (frame, `tinymd::PageNavigator`), `ServicePage` (a `tinymd::Page`), `ClockPage`, `SourcesPage`, `PublishPage`, `MetricsPage`, `SettingsPage` |

The blocking sources (NTP queries, BLE scans) run in their own tasks. The engine is thread safe.

```cpp
#include "TimeServices.h"

TimeEngine engine;
NtpSources<WiFiUDP> ntpSources(engine);     // network task: ntpSources.update()
NtpPublisher<WiFiUDP> ntpServer(engine);
TimeServicesApp<RGB565> gui(board, engine);

void setup() {
  engine.begin(defaultConfig());
  gui.begin();
}

void loop() {
  engine.tick();
  ntpServer.process();
  gui.update();
}
```

## Examples

- [desktop-simulator](examples/desktop-simulator): the GUI in an SDL2 window. It makes real NTP queries and runs an NTP server on port 12123.
- [esp32-s3-hosyond](examples/esp32-s3-hosyond): ESP32-S3 2.8" display with WiFi (NTP), Bluetooth (CTS client and server), an optional GNSS receiver and settings stored in flash.

## Desktop build

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/examples/desktop-simulator
```

The dependencies are fetched from GitHub. Use `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>` to use local checkouts. Set `TS_SCREENSHOTS=<dir>` to save a screenshot of every page.

## Differences to the Android app

- The device clock can be set (`stepSystemClock`). Android only estimates the offset.
- GNSS comes from an NMEA receiver with optional PPS (TinyNTP's `TinyGPSTimeSource`), not from the Location API or the raw GNSS clock.
- English only, with no notifications. Alerts are shown on the Clock page and reported to `TimeEngine::onAlert`.
- Not ported: the NTP analyser and the self test of the NTP server, the keep-awake setting, and the PPS click sound.

## Installation in Arduino

Clone this project and the dependencies into the Arduino libraries folder:

- [TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign) and [TinyGPU](https://github.com/pschatzmann/TinyGPU)
- [TinyNTP](https://github.com/pschatzmann/TinyNTP) (GPL v3)
- [TinyCTS](https://github.com/pschatzmann/TinyCTS) (GPL v3) and [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) on the ESP32: needed only for Bluetooth

## License

[MIT](LICENSE)

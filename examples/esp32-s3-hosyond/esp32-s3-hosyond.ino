/**
 * @file esp32-s3-hosyond.ino
 * @brief Time Services on the ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3
 * Hosyond Display): ILI9341 240x320 touch display.
 *
 * - WiFi: on startup the WiFi page (networks, password) is shown when no
 *   network is stored or the stored one does not connect; later it is in the
 *   menu. The credentials are stored in the flash when the connection works.
 * - The NTP servers of the configuration are queried over WiFi (TinyNTP) in
 *   their own task; the fused time is served as NTP server on port 123.
 * - Bluetooth LE (TinyCTS with NimBLE-Arduino): a Current Time Service peer
 *   as time source (in its own task) and the Current Time Service peripheral
 *   "TimeServices".
 * - Optional GNSS receiver: NMEA on Serial1 (kGnssRx/kGnssTx) and PPS on
 *   kGnssPps; set kGnssRx to -1 if none is connected.
 * - The system clock of the ESP32 is set to the fused time, so time() and
 *   gettimeofday() are correct as well.
 * - The settings are stored in the flash (Preferences).
 * - "Shut down" in the menu switches off the display and puts the ESP32 into
 *   deep sleep; press the reset button to start again.
 *
 * Dependencies: Arduino-TimeServices, TinyMaterialDesign, TinyGPU, TinyNTP,
 * TinyCTS and NimBLE-Arduino.
 *
 * Arduino IDE: board "ESP32S3 Dev Module", PSRAM "OPI PSRAM", Flash Size
 * "16MB", Partition Scheme "Huge APP".
 */
#include <Preferences.h>
#include <SPI.h>  // display (TinyGPU)
#include <Wire.h>  // touch (TinyGPU)
#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "TinyCTS.h"
#include "TinyNTP.h"
#include "TimeServices.h"
#include "TimeServices/BleCts.h"
#include "TinyMaterialDesign/Wifi/WifiEsp32.h"

// GNSS receiver: -1 = not connected
const int kGnssRx = -1;
const int kGnssTx = -1;
const int kGnssPps = -1;
const uint32_t kGnssBaud = 9600;

ESP32S3HosyondLCD board;  // display + touch (TinyGPU)
TimeEngine engine;
NtpSources<WiFiUDP> ntpSources(engine);
NtpPublisher<WiFiUDP> ntpPublisher(engine);
CtsSource ctsSource(engine);
CtsPublisher ctsPublisher(engine, "TimeServices");
GnssSource gnss(engine, Serial1, kGnssPps);
TimeServicesApp<RGB565> gui(board, engine);
Esp32Wifi wifi;  // WiFi page: scan, login, stored credentials
Preferences prefs;

/// Settings stored in the flash; the version invalidates old layouts.
const uint32_t kConfigVersion = 2;

SystemConfig loadConfig(bool& dark) {
  SystemConfig cfg = defaultConfig();
  cfg.stepSystemClock = true;
  prefs.begin("timeservices", true);
  if (prefs.getUInt("version", 0) == kConfigVersion && prefs.getBytesLength("config") == sizeof(cfg)) {
    prefs.getBytes("config", &cfg, sizeof(cfg));
  }
  dark = prefs.getBool("dark", true);
  prefs.end();
  return cfg;
}

void saveConfig(const SystemConfig& cfg, bool dark) {
  prefs.begin("timeservices", false);
  prefs.putUInt("version", kConfigVersion);
  prefs.putBytes("config", &cfg, sizeof(cfg));
  prefs.putBool("dark", dark);
  prefs.end();
  Serial.println("Settings saved");
}

/// Blocking NTP queries (core 0).
void ntpTask(void*) {
  for (;;) {
    engine.setNetworkAvailable(WiFi.status() == WL_CONNECTED);
    ntpSources.update();
    delay(100);
  }
}

/// Blocking BLE scans of the CTS source (core 0).
void bleTask(void*) {
  for (;;) {
    ctsSource.update();
    delay(200);
  }
}

TaskHandle_t ntpHandle = nullptr, bleHandle = nullptr;

/// Switches off the backlight, then sleeps until reset.
void shutdown() {
  Serial.println("Shutting down");
  if (ntpHandle) vTaskDelete(ntpHandle);
  if (bleHandle) vTaskDelete(bleHandle);
  WiFi.disconnect(true);
  const gpio_num_t backlight = static_cast<gpio_num_t>(board.backlightPin());
  digitalWrite(backlight, LOW);
  gpio_hold_en(backlight);  // keeps the backlight off during the deep sleep
  gpio_deep_sleep_hold_en();
  Serial.flush();
  esp_deep_sleep_start();  // no wake-up source: only the reset button
}

void setup() {
  Serial.begin(115200);
  bool dark = true;
  engine.begin(loadConfig(dark));
  engine.setNetworkAvailable(false);
  engine.setBluetoothAvailable(true);
  engine.onAlert = [](const Alert& a, bool raised) {
    Serial.printf("%s: %s - %s\n", raised ? "ALERT" : "cleared", a.title, a.text);
  };

  gpio_hold_dis(static_cast<gpio_num_t>(board.backlightPin()));  // set by shutdown()
  wifi.begin();  // connects with the stored credentials (if any)
  gui.onSettingsChanged = saveConfig;
  gui.onShutdown = shutdown;
  gui.setWifi(wifi);  // WiFi page in the menu and on startup when not connected
  gui.begin(dark);

  if (kGnssRx >= 0) {
    Serial1.begin(kGnssBaud, SERIAL_8N1, kGnssRx, kGnssTx);
    gnss.begin();
  }
  xTaskCreatePinnedToCore(ntpTask, "ntp", 6144, nullptr, 1, &ntpHandle, 0);
  xTaskCreatePinnedToCore(bleTask, "ble", 6144, nullptr, 1, &bleHandle, 0);
}

void loop() {
  engine.tick();
  gnss.process();
  ntpPublisher.process();
  ctsPublisher.process();
  gui.update();
}

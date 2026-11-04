/**
 * @file desktop-simulator.ino
 * @brief The Time Services GUI in a desktop SDL2 window (mouse = touch).
 *
 * The NTP sources are queried for real over the network of the computer
 * (TinyNTP with the UDP class of the Arduino emulator) in their own thread.
 * The NTP server listens on the NTP port 123. On Linux the program needs
 * the right to use it (ports below 1024 are privileged), e.g.
 *   sudo setcap 'cap_net_bind_service=+ep' build/examples/desktop-simulator
 * (again after each build); otherwise the Publish tab shows that the port
 * could not be opened. The environment variable TS_NTP_PORT selects another
 * port (e.g. 12123). The system clock of the computer is only measured,
 * never set. There is no GNSS receiver and no Bluetooth.
 *
 * The WiFi page works with a simulated WiFi (the password of the secure
 * networks is "demo"). It starts connected; with the environment variable
 * TS_WIFI_STARTUP set it starts without a network, so the WiFi page is shown
 * on startup.
 *
 * With the environment variable TS_SCREENSHOTS set to a directory, the
 * sources are queried for 20 s and then every page is saved there as a BMP
 * image (TinyMaterialDesign's App::saveScreenshot()).
 */
#include <stdio.h>
#include <stdlib.h>

#include <atomic>
#include <thread>

#include "TimeServices.h"
#include "WiFiUdp.h"

TimeEngine engine;
NtpSources<WiFiUDP> ntpSources(engine);
NtpPublisher<WiFiUDP> ntpPublisher(engine);
TimeServicesApp<RGB565> gui(DefaultBoard, engine);
SimulatedWifi wifi;
std::atomic<bool> running{true};
std::thread ntpThread;

/// The blocking NTP queries run in their own thread.
void ntpLoop() {
  while (running) {
    ntpSources.update();
    delay(100);
  }
}

void stop() {
  running = false;
  if (ntpThread.joinable()) ntpThread.join();
}

void savePage(const char* dir, const char* name) {
  // let the GUI pick up the data of the page
  for (int i = 0; i < 4; ++i) {
    engine.tick();
    gui.update();
    delay(120);
  }
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
  FileStream file(path);
  if (gui.app().saveScreenshot(file)) printf("saved %s\n", path);
}

void screenshots(const char* dir) {
  const uint32_t start = millis();
  while (millis() - start < 20000) {
    engine.tick();
    ntpPublisher.process();
    gui.update();
    delay(20);
  }
  const char* names[] = {"clock", "sources", "publish", "metrics", "settings"};
  for (int page = 0; page < 5; ++page) {
    gui.showPage(page);
    savePage(dir, names[page]);
  }
  gui.showWifi();
  delay(900);  // the simulated scan
  savePage(dir, "wifi");
  gui.setDark(false);
  gui.showPage(0);
  savePage(dir, "clock-light");
  stop();
  exit(0);
}

void setup() {
  Serial.begin(115200);
  SystemConfig cfg = defaultConfig();
  // the NTP port 123, or TS_NTP_PORT (no privileges needed above 1023)
  const char* port = getenv("TS_NTP_PORT");
  if (port != nullptr && atoi(port) > 0 && atoi(port) < 65536) cfg.ntpServer.listenPort = static_cast<uint16_t>(atoi(port));
  cfg.ntpServer.enabled = true;
  copyText(cfg.timeZone, "Europe/Zurich");
  engine.begin(cfg);
  engine.setNetworkAvailable(true);
  engine.setBluetoothAvailable(false);
  engine.onAlert = [](const Alert& a, bool raised) {
    printf("%s: %s - %s\n", raised ? "ALERT" : "cleared", a.title, a.text);
  };

  if (getenv("TS_WIFI_STARTUP") == nullptr) wifi.setConnected("HomeNetwork");
  gui.setWifi(wifi);
  gui.onShutdown = []() {
    stop();
    exit(0);
  };
  gui.begin();
  ntpThread = std::thread(ntpLoop);

  const char* dir = getenv("TS_SCREENSHOTS");
  if (dir != nullptr) screenshots(dir);
}

void loop() {
  engine.tick();
  ntpPublisher.process();
  gui.update();
}

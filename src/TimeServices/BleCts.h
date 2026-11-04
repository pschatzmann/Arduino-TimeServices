#pragma once
/**
 * @file BleCts.h
 * @brief The Bluetooth LE Current Time Service as time source (CtsSource,
 * TinyCTSClient) and as publisher (CtsPublisher, TinyCTSServer). Not part of
 * TimeServices.h: include it after TinyCTS.h when you use Bluetooth. The
 * Bluetooth stack is selected by TinyCTS (NimBLE-Arduino on the ESP32).
 */
#include <Arduino.h>

#include <memory>
#include <string>

#include "TimeServices/TimeEngine.h"
#include "TinyCTS.h"

#if defined(TINY_CTS_NO_BLE)
#error "BleCts.h needs the Bluetooth classes of TinyCTS (TINY_CTS_NO_BLE is defined)"
#endif

namespace time_services {

/**
 * @brief A peer that offers the Current Time Service (a phone, a watch,
 * another board) as time source: TinyCTSClient.
 *
 * update() scans for the peer (SourceConfig::host: address or name; empty =
 * the first device with the service), reads the time and disconnects. It
 * blocks for the scan time, so call it from its own task:
 *
 *   CtsSource ctsSource(engine);
 *   // Bluetooth task:
 *   ctsSource.update();
 *
 * The delay is the round trip of the read of the Current Time
 * (TinyCTSClient::getRoundTripMs()).
 */
class CtsSource {
 public:
  explicit CtsSource(TimeEngine& engine, uint32_t scanMs = 5000) : engine_(engine), scanMs_(scanMs) {}

  /// Reads the time if the source is due. Returns true if a sample was added.
  bool update() {
    const SystemConfig cfg = engine_.config();
    const SourceConfig* src = cfg.firstOfType(SourceType::kBleCts, true);
    if (src == nullptr) {
      if (client_) {
        client_.reset();
        engine_.setBleClientState("idle");
      }
      return false;
    }
    if (!engine_.isDue(src->id)) return false;
    if (!client_ || device_ != src->host) {
      device_ = src->host;
      client_.reset(new tiny_cts::TinyCTSClient(device_.empty() ? nullptr : device_.c_str(), scanMs_));
      begun_ = false;
    }
    engine_.setBleClientState(device_.empty() ? "scanning" : "connecting");
    const bool ok = begun_ ? client_->update() : client_->begin();
    if (!ok) {
      const std::string& error = client_->getLastError();
      engine_.setBleClientState(error.empty() ? "failed" : error.c_str());
      engine_.recordError(src->id, error.empty() ? "no peer with the Current Time Service" : error.c_str());
      return false;
    }
    begun_ = true;
    const double peer = static_cast<double>(client_->getTimeMs());
    const double local = engine_.clock().nowMs();
    const double rtt = client_->getRoundTripMs();
    char detail[64];
    snprintf(detail, sizeof(detail), "peer %s %s", client_->getDeviceAddress().c_str(),
             client_->getDeviceName().c_str());
    engine_.recordSample(src->id, peer - local, rtt, detail);
    char state[48];
    snprintf(state, sizeof(state), "read %s", client_->getDeviceAddress().c_str());
    engine_.setBleClientState(state);
    return true;
  }

  tiny_cts::TinyCTSClient* client() { return client_.get(); }

 protected:
  TimeEngine& engine_;
  uint32_t scanMs_;
  std::string device_;
  std::unique_ptr<tiny_cts::TinyCTSClient> client_;
  bool begun_ = false;
};

/**
 * @brief Publishes the clock of the TimeEngine (TimeEngine::published()) as a
 * BLE Current Time Service peripheral: TinyCTSServer. The Local Time
 * Information follows the time zone of the configuration, the Reference Time
 * Information the published source.
 *
 * Call process() in loop(): it starts / stops the server (TinyCTSServer
 * begin() / end()) when the configuration or Bluetooth changes.
 */
class CtsPublisher : public tiny_cts::TinyCTSTimeSource {
 public:
  explicit CtsPublisher(TimeEngine& engine, const char* deviceName = "TimeServices")
      : engine_(engine), name_(deviceName) {}

  uint64_t getTimeMs() override {
    const Published p = engine_.published();
    return p.stratum >= 16 || p.nowMs <= 0 ? 0 : static_cast<uint64_t>(p.nowMs);
  }

  void process() {
    if (running_) server_.process();
    const uint32_t now = millis();
    if (checked_ && now - lastCheckMs_ < 500) return;
    checked_ = true;
    lastCheckMs_ = now;
    reconcile();
  }

  tiny_cts::TinyCTSServer& server() { return server_; }

 protected:
  TimeEngine& engine_;
  const char* name_;
  tiny_cts::TinyCTSServer server_;
  bool running_ = false;
  uint16_t advMs_ = 0;
  bool checked_ = false;
  uint32_t lastCheckMs_ = 0;
  int lastZoneMinutes_ = -100000;
  int lastDstMinutes_ = -1;
  uint8_t lastRef_ = 255, lastAccuracy_ = 0, lastHours_ = 255;

  void reconcile() {
    const SystemConfig cfg = engine_.config();
    const bool want = cfg.bleCts.enabled && engine_.bluetoothAvailable();
    BleServerState st;
    st.waitingForBluetooth = cfg.bleCts.enabled && !engine_.bluetoothAvailable();

    server_.setNotifyInterval(cfg.bleCts.sendNotifications ? std::max<uint32_t>(100, cfg.bleCts.notifyIntervalMs) : 0);
    const Published p = engine_.published();
    updateInfo(p);

    if (running_ && (!want || cfg.bleCts.advIntervalMs != advMs_)) {
      server_.end();
      running_ = false;
    }
    if (want && !running_) {
      advMs_ = cfg.bleCts.advIntervalMs;
      server_.setAdvertisingInterval(advMs_);
      running_ = server_.begin(*this, name_);
      if (!running_) copyText(st.error, "Bluetooth service could not be started");
    }
    st.running = running_;
    if (running_) st.connected = server_.getConnectedCount();
    // preview of the Current Time characteristic
    if (p.nowMs > 0) {
      uint8_t data[tiny_cts::TinyCTSCurrentTime::Size];
      const size_t n = tiny_cts::TinyCTSCurrentTime::fromUnixMs(static_cast<uint64_t>(p.nowMs)).encode(data);
      int len = 0;
      for (size_t i = 0; i < n && len < static_cast<int>(sizeof(st.lastPacketHex)) - 3; ++i) {
        len += snprintf(st.lastPacketHex + len, sizeof(st.lastPacketHex) - len, i == 0 ? "%02x" : " %02x", data[i]);
      }
    }
    engine_.setBleServerState(st);
  }

  /// Time zone (Local Time Information) and Reference Time Information.
  void updateInfo(const Published& p) {
    const TimeZoneInfo zone = engine_.timeZone();
    const TimeZoneDetails d = zone.details(static_cast<int64_t>(p.nowMs / 1000.0));
    const int zoneMinutes = zone.standardOffsetSeconds() / 60;
    if (zoneMinutes != lastZoneMinutes_ || d.dstMinutes != lastDstMinutes_) {
      lastZoneMinutes_ = zoneMinutes;
      lastDstMinutes_ = d.dstMinutes;
      server_.setTimeZone(zoneMinutes, d.dstMinutes);
    }
    // accuracy in 125 ms steps (254: more than 31.5 s, 255: unknown)
    const double acc = p.stratum >= 16 ? -1 : ceil(p.rootDispMs / 125.0);
    const uint8_t accuracy = acc < 0 ? 255 : acc > 253 ? 254 : static_cast<uint8_t>(acc);
    // days and hours since the clock was disciplined last
    const double sinceMs = p.lastDisciplineMs > 0 ? p.nowMs - p.lastDisciplineMs : -1;
    const uint8_t hours = sinceMs < 0 ? 255 : static_cast<uint8_t>(sinceMs / 3600000.0 > 254 ? 254 : sinceMs / 3600000.0);
    if (p.refSource != lastRef_ || accuracy != lastAccuracy_ || hours != lastHours_) {
      lastRef_ = p.refSource;
      lastAccuracy_ = accuracy;
      lastHours_ = hours;
      server_.setReferenceInfo(p.refSource, accuracy);
      if (sinceMs >= 0) server_.setTimeSinceUpdateMs(static_cast<uint64_t>(sinceMs));
    }
  }
};

}  // namespace time_services

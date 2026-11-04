#pragma once
#include <Arduino.h>

#include "TimeServices/TimeEngine.h"
#include "TinyNTP.h"

namespace time_services {

/**
 * @brief Publishes the clock of the TimeEngine (see TimeEngine::published())
 * as an NTP server: a TinyNTPServer (TinyNTP) with the settings of
 * NtpServerConfig (port, stratum, reference id, leap indicator, allowed
 * subnets, rate limit). Root delay and dispersion describe the published
 * clock.
 *
 * Call process() in loop(): it starts / stops the server when the
 * configuration or the network changes and answers the requests.
 *
 * While the published clock is not synchronised the answers have stratum 16
 * and leap indicator 3, so the clients ignore them (as the Android app).
 *
 * @tparam UDPAPI The UDP class (e.g. WiFiUDP)
 */
template <typename UDPAPI>
class NtpPublisher : public tiny_ntp::TinyTimeSource {
 public:
  explicit NtpPublisher(TimeEngine& engine) : engine_(engine) {}

  /// The published time.
  uint64_t getTimeMs() override {
    const Published p = engine_.published();
    return p.nowMs > 0 ? static_cast<uint64_t>(p.nowMs) : 0;
  }

  /// Applies the configuration and answers pending requests.
  void process() {
    const uint32_t now = millis();
    if (!checked_ || now - lastCheckMs_ >= 1000) {
      checked_ = true;
      lastCheckMs_ = now;
      reconcile();
    }
    if (!running_) return;
    bool answered = false;
    while (server_.process()) answered = true;  // all pending requests
    if (answered || server_.getDroppedCount() != reportedDropped_) report();
  }

  tiny_ntp::TinyNTPServer<UDPAPI>& server() { return server_; }

 protected:
  TimeEngine& engine_;
  tiny_ntp::TinyNTPServer<UDPAPI> server_;
  static constexpr uint32_t kRetryMs = 30000;
  bool running_ = false;
  uint32_t failedMs_ = 0;  // millis() of the last failed begin() (0 = none)
  bool checked_ = false;
  uint32_t lastCheckMs_ = 0;
  uint16_t port_ = 0;
  uint32_t reportedDropped_ = 0;
  NtpServerConfig active_;

  void reconcile() {
    const SystemConfig cfg = engine_.config();
    const bool online = engine_.networkAvailable();
    const bool want = cfg.ntpServer.enabled && online;
    const bool changed = cfg.ntpServer.listenPort != active_.listenPort;
    if (changed || !want) failedMs_ = 0;  // a new port or state: try at once
    if (running_ && (!want || changed)) {
      server_.end();
      running_ = false;
    }
    active_ = cfg.ntpServer;
    server_.setAllowedSubnets(cfg.ntpServer.allowSubnets);
    server_.setRateLimit(cfg.ntpServer.rateLimitPerMin);
    // the header describes the published clock
    const Published p = engine_.published();
    const bool synced = p.stratum < 16;
    server_.setStratum(static_cast<uint8_t>(synced ? p.stratum : 16));
    server_.setLeapIndicator(synced ? cfg.ntpServer.leapIndicator : 3);
    server_.setReferenceId(cfg.ntpServer.refId);
    server_.setRootDelayMs(p.rootDelayMs);
    server_.setRootDispersionMs(p.rootDispMs);
    server_.setReferenceTimeMs(p.lastDisciplineMs > 0 ? static_cast<uint64_t>(p.lastDisciplineMs) : 0);

    NtpServerState st = engine_.ntpServerState();
    // a port that could not be opened is tried again after kRetryMs
    const uint32_t now = millis();
    const bool retryDue = failedMs_ == 0 || now - failedMs_ >= kRetryMs;
    if (want && !running_ && retryDue) {
      running_ = server_.begin(*this, cfg.ntpServer.listenPort);
      failedMs_ = running_ ? 0 : (now == 0 ? 1 : now);
      port_ = cfg.ntpServer.listenPort;
      st = NtpServerState();
      st.port = port_;
      st.running = running_;
      if (!running_) snprintf(st.error, sizeof(st.error), "UDP port %u could not be opened", port_);
      engine_.setNtpServerState(st);
    } else if (!want) {
      st = NtpServerState();
      st.port = cfg.ntpServer.listenPort;
      st.waitingForNetwork = cfg.ntpServer.enabled && !online;
      engine_.setNtpServerState(st);
    }
  }

  void report() {
    NtpServerState st;
    st.running = true;
    st.port = port_;
    st.packetsServed = server_.getRequestCount();
    st.dropped = reportedDropped_ = server_.getDroppedCount();
    copyText(st.lastClient, server_.getLastClient());
    engine_.setNtpServerState(st);
  }
};

}  // namespace time_services

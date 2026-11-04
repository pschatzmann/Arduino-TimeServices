#pragma once
#include <Arduino.h>

#include <memory>
#include <string>
#include <vector>

#include "TimeServices/TimeEngine.h"
#include "TinyNTP.h"

namespace time_services {

/**
 * @brief The NTP servers of the configuration as time sources: one
 * TinyNTPClient (TinyNTP) per server.
 *
 * update() queries every server whose poll interval has passed and reports
 * the result to the TimeEngine: the offset is the time of the server
 * (TinyNTPClient::getLastResult()) minus the local clock at the moment the
 * answer arrived, the delay is the round trip without the processing time of
 * the server. Kiss-o'-death and unsynchronised servers are reported as
 * errors.
 *
 * update() blocks until the servers have answered (at most timeoutMs each),
 * so call it from its own task, not from the GUI loop:
 *
 *   NtpSources<WiFiUDP> ntpSources(engine);
 *   // network task:
 *   ntpSources.update();
 *
 * @tparam UDPAPI The UDP class (e.g. WiFiUDP)
 */
template <typename UDPAPI>
class NtpSources {
 public:
  /**
   * @param engine The engine that gets the readings.
   * @param firstLocalPort Each client sends from its own local port, starting
   * here (so that a TinyNTPServer can use port 123 on the same device).
   * @param timeoutMs Time to wait for an answer.
   */
  explicit NtpSources(TimeEngine& engine, uint16_t firstLocalPort = 50123, uint32_t timeoutMs = 2500)
      : engine_(engine), nextPort_(firstLocalPort), timeoutMs_(timeoutMs) {}

  /// Queries the servers that are due. Returns the number of queries.
  int update() {
    const SystemConfig cfg = engine_.config();
    removeUnused(cfg);
    int count = 0;
    for (int i = 0; i < cfg.sourceCount; ++i) {
      const SourceConfig& s = cfg.sources[i];
      if (s.type != SourceType::kNtp || !s.enabled) continue;
      if (!isValidNtpHost(s.host)) {
        engine_.recordError(s.id, "invalid host");
        continue;
      }
      if (!engine_.isDue(s.id)) continue;
      query(entryFor(s), s);
      count++;
    }
    return count;
  }

 protected:
  struct Entry {
    std::string id;
    std::string hostPort;
    std::string host;
    std::unique_ptr<tiny_ntp::TinyNTPClient<UDPAPI>> client;
  };

  TimeEngine& engine_;
  uint16_t nextPort_;
  uint32_t timeoutMs_;
  std::vector<std::unique_ptr<Entry>> entries_;

  Entry& entryFor(const SourceConfig& s) {
    for (auto& e : entries_) {
      if (e->id == s.id) {
        if (e->hostPort != s.host) configure(*e, s);
        return *e;
      }
    }
    entries_.emplace_back(new Entry());
    Entry& e = *entries_.back();
    e.id = s.id;
    e.client.reset(new tiny_ntp::TinyNTPClient<UDPAPI>(nullptr, 123, timeoutMs_));
    e.client->setLocalPort(nextPort_++);
    configure(e, s);
    return e;
  }

  void configure(Entry& e, const SourceConfig& s) {
    e.hostPort = s.host;
    char host[64];
    int port;
    splitHostPort(s.host, host, sizeof(host), port);
    e.host = host;
    e.client->setServer(e.host.c_str(), port);
    e.client->end();  // a new server: no correction factor from the old one
  }

  void removeUnused(const SystemConfig& cfg) {
    for (size_t i = 0; i < entries_.size();) {
      const SourceConfig* s = cfg.find(entries_[i]->id.c_str());
      if (s == nullptr || s->type != SourceType::kNtp) {
        entries_.erase(entries_.begin() + i);
      } else {
        ++i;
      }
    }
  }

  void query(Entry& e, const SourceConfig& s) {
    tiny_ntp::TinyNTPClient<UDPAPI>& client = *e.client;
    const bool ok = client ? client.update() : client.begin();
    const tiny_ntp::TinyNTPResult& r = client.getLastResult();
    char msg[64];
    if (!ok) {
      if (r.stratum == 0 && r.referenceText[0] != 0) {
        snprintf(msg, sizeof(msg), "kiss-o'-death %s from %s", r.referenceText, e.host.c_str());
      } else if (r.leapIndicator == 3 || r.stratum >= 16) {
        snprintf(msg, sizeof(msg), "%s is not synchronised", e.host.c_str());
      } else {
        snprintf(msg, sizeof(msg), "no answer from %s", e.host.c_str());
      }
      engine_.recordError(s.id, msg);
      return;
    }
    // the time of the server when the answer was received, compared with
    // the local clock at that moment
    const double local = engine_.clock().nowMs() - static_cast<double>(static_cast<uint32_t>(millis() - r.receivedMillis));
    snprintf(msg, sizeof(msg), "stratum %u, ref %s", r.stratum, r.referenceText);
    engine_.recordSample(s.id, static_cast<double>(r.serverTimeMs) - local, r.delayMs, msg);
  }
};

}  // namespace time_services

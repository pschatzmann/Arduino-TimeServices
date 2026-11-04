#pragma once
#include <Arduino.h>

#include "TimeServices/TimeEngine.h"
#include "TinyNTP.h"

// (the desktop emulator has no interrupts)
#ifndef digitalPinToInterrupt
#define digitalPinToInterrupt(p) (p)
#endif

#if defined(ESP32) || defined(ESP8266)
#define TS_ISR_ATTR IRAM_ATTR
#else
#define TS_ISR_ATTR
#endif

namespace time_services {

/**
 * @brief A GNSS receiver as time source: the NMEA sentences from a serial
 * port and, optionally, the pulse per second (PPS) on a pin. The decoding is
 * done by TinyNTP's TinyGPSTimeSource:
 *
 * - With PPS, the rising edge marks the start of the second that the
 *   following RMC/ZDA sentence reports: the offset is precise to a few µs.
 * - Without PPS, the arrival of the first character of the sentence is used,
 *   corrected by setLatencyMs(): precise to tens of ms (like the Location
 *   time of the Android app).
 *
 * The first enabled GNSS source of the configuration gets the readings.
 * Call process() in loop() (it does not block):
 *
 *   GnssSource gnss(engine, Serial1, PPS_PIN);
 *   void setup() { Serial1.begin(9600); gnss.begin(); }
 *   void loop() { gnss.process(); }
 *
 * The TinyGPSTimeSource (gps()) can also be served directly by a
 * TinyNTPServer.
 */
class GnssSource {
 public:
  GnssSource(TimeEngine& engine, Stream& in, int ppsPin = -1) : engine_(engine), in_(in), ppsPin_(ppsPin) {}

  /// Time between the start of the second and the sentence (without PPS).
  void setLatencyMs(uint32_t ms) { gps_.setLatencyMs(ms); }

  void begin() {
    if (ppsPin_ >= 0) {
      instance_ = this;
      pinMode(ppsPin_, INPUT);
      attachInterrupt(digitalPinToInterrupt(ppsPin_), ppsIsr, RISING);
    }
    started_ = true;
    publishState();
  }

  void end() {
    if (ppsPin_ >= 0) detachInterrupt(digitalPinToInterrupt(ppsPin_));
    started_ = false;
    engine_.setGnssState(GnssState());
  }

  /// Reads the available characters of the receiver.
  void process() {
    if (!started_) return;
    gps_.process(in_);
    if (gps_.getFixCount() != lastFixCount_) {
      lastFixCount_ = gps_.getFixCount();
      report();
    }
    const uint32_t now = millis();
    if (now - lastStateMs_ >= 1000) {
      lastStateMs_ = now;
      publishState();
    }
  }

  /// The TinyNTP time source of the receiver.
  tiny_ntp::TinyGPSTimeSource& gps() { return gps_; }

 protected:
  TimeEngine& engine_;
  Stream& in_;
  int ppsPin_;
  bool started_ = false;
  tiny_ntp::TinyGPSTimeSource gps_;
  uint32_t lastFixCount_ = 0;
  uint32_t lastStateMs_ = 0;
  static inline GnssSource* instance_ = nullptr;

  static void TS_ISR_ATTR ppsIsr() {
    if (instance_ != nullptr) instance_->gps_.pps(micros());
  }

  void publishState() {
    GnssState st;
    const tiny_ntp::TinyNMEAParser& p = gps_.parser();
    st.available = started_;
    st.hasFix = p.hasFix();
    st.satsUsed = p.getSatellitesUsed();
    st.satsVisible = p.getSatellitesVisible();
    st.ppsConfigured = ppsPin_ >= 0;
    st.ppsActive = gps_.isPpsActive();
    engine_.setGnssState(st);
  }

  /// A new fix: UTC time at a micros() value, compared with the local clock.
  void report() {
    const SystemConfig cfg = engine_.config();
    const SourceConfig* src = cfg.firstOfType(SourceType::kGnss, true);
    if (src == nullptr) return;
    char id[16];
    copyText(id, src->id);
    uint64_t utcMs;
    uint32_t atMicros;
    bool pps;
    if (!gps_.getLastFix(utcMs, atMicros, pps)) return;
    const double localAtFix = engine_.clock().nowMs() - static_cast<uint32_t>(micros() - atMicros) / 1000.0;
    const tiny_ntp::TinyNMEAParser& p = gps_.parser();
    char detail[64];
    snprintf(detail, sizeof(detail), "%s, %d/%d satellites", pps ? "PPS" : "NMEA", p.getSatellitesUsed(),
             p.getSatellitesVisible());
    engine_.recordSample(id, static_cast<double>(utcMs) - localAtFix, NAN, detail);
  }
};

}  // namespace time_services

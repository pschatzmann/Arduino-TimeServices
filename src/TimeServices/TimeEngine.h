#pragma once
#include <Arduino.h>
#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "TimeServices/LocalClock.h"
#include "TimeServices/Models.h"
#include "TimeServices/TimeServicesConfig.h"
#include "TimeServices/TimeZoneInfo.h"

namespace time_services {

/**
 * @brief Fixed capacity ring buffer: index 0 is the oldest entry.
 */
template <typename T>
class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity = 0) { data_.resize(capacity); }
  void push(const T& v) {
    if (data_.empty()) return;
    if (size_ < data_.size()) {
      data_[(start_ + size_++) % data_.size()] = v;
    } else {
      data_[start_] = v;
      start_ = (start_ + 1) % data_.size();
    }
  }
  const T& operator[](size_t i) const { return data_[(start_ + i) % data_.size()]; }
  T& operator[](size_t i) { return data_[(start_ + i) % data_.size()]; }
  size_t size() const { return size_; }
  void clear() { start_ = size_ = 0; }

 private:
  std::vector<T> data_;
  size_t start_ = 0, size_ = 0;
};

/**
 * @brief The time engine (port of the Android TimeEngine): it collects the
 * readings of the time sources, rejects outliers, fuses them into one clock
 * offset (weighted mean, median or PLL/FLL smoothing), keeps the clock in
 * holdover when all sources are lost and provides the clock that the NTP
 * and BLE servers publish.
 *
 * The engine does no I/O itself: the sources (NtpSources, GnssSource,
 * CtsSource) report their readings with recordSample() / recordError(), the
 * publishers (NtpPublisher, CtsPublisher) read published(). Call tick()
 * regularly (e.g. in loop()): the fusion runs once per second.
 *
 * All offsets are "true time - local clock" in ms. The local clock is the
 * system clock (see LocalClock): with SystemConfig::stepSystemClock it is
 * set to the fused time, otherwise it is left alone (as on Android, where
 * an app can not set the clock).
 *
 * All public methods are thread safe: the blocking sources can run in their
 * own tasks.
 */
class TimeEngine {
 public:
  explicit TimeEngine(LocalClock& clock) : clock_(&clock), history_(TS_MAX_HISTORY) {}
  TimeEngine() : clock_(&systemClock_), history_(TS_MAX_HISTORY) {}

  /// Called when an alert is raised (true) or cleared (false).
  std::function<void(const Alert& alert, bool raised)> onAlert;

  void begin(const SystemConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    zone_.select(config_.timeZone);
    revision_++;
  }

  SystemConfig config() {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
  }

  void setConfig(const SystemConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    zone_.select(config_.timeZone);
    // sources that were removed lose their state
    runtime_.erase(std::remove_if(runtime_.begin(), runtime_.end(),
                                  [&](const Runtime& r) { return config_.find(r.id) == nullptr; }),
                   runtime_.end());
    revision_++;
  }

  LocalClock& clock() { return *clock_; }

  /// Local clock + fused offset: the best estimate of the true UTC time (ms).
  double nowMs() {
    const double offset = fusedOffset();
    return clock_->nowMs() + offset;
  }

  double fusedOffset() {
    std::lock_guard<std::mutex> lock(mutex_);
    return fusedOffsetMs_;
  }

  // ------------------------------------------------------------ transports

  /// WiFi / Ethernet is up: the NTP sources and the NTP server need it.
  void setNetworkAvailable(bool up) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (up && !network_) regained_ = true;
    if (up != network_) revision_++;
    network_ = up;
  }
  bool networkAvailable() {
    std::lock_guard<std::mutex> lock(mutex_);
    return network_;
  }

  /// Bluetooth can be used (BLE CTS source and server).
  void setBluetoothAvailable(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    bluetooth_ = on;
  }
  bool bluetoothAvailable() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bluetooth_;
  }

  // --------------------------------------------------------- source input

  /**
   * @brief True if the source should be polled now (enabled, its transport
   * is up and the poll interval has passed; 5 s after a failure). The poll
   * is then counted as started.
   */
  bool isDue(const char* id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const SourceConfig* src = config_.find(id);
    if (src == nullptr || !src->enabled || radioOff(*src)) return false;
    Runtime& rt = runtimeFor(id);
    const uint64_t now = monoMs();
    const uint64_t minPoll = src->type == SourceType::kNtp ? 4 : 1;
    const uint64_t pollMs = std::max<uint64_t>(minPoll, src->pollIntervalSec) * 1000ULL;
    const uint64_t dueMs = rt.error[0] != 0 ? std::min<uint64_t>(pollMs, 5000) : pollMs;
    if (rt.hasPolled && now - rt.lastPollMs < dueMs) return false;
    rt.hasPolled = true;
    rt.lastPollMs = now;
    return true;
  }

  /**
   * @brief Reports a reading of a source.
   * @param offsetMs true time - local clock
   * @param delayMs round trip (NaN if not known)
   * @param detail text for the Sources page (e.g. "4 satellites, PPS")
   */
  void recordSample(const char* id, double offsetMs, double delayMs, const char* detail = nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    Runtime& rt = runtimeFor(id);
    rt.offsets.push(offsetMs);
    rt.hasSample = true;
    rt.offsetMs = offsetMs;
    rt.delayMs = delayMs;
    rt.atMono = monoMs();
    rt.error[0] = 0;
    if (detail != nullptr) copyText(rt.detail, detail);
    revision_++;
  }

  /// Reports a failed reading of a source.
  void recordError(const char* id, const char* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    copyText(runtimeFor(id).error, error);
    revision_++;
  }

  /// Sets the detail text of a source without a reading.
  void setDetail(const char* id, const char* detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    copyText(runtimeFor(id).detail, detail);
  }

  /// Polls all sources at the next opportunity.
  void forceSync() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Runtime& rt : runtime_) rt.hasPolled = false;
    syncUntilMs_ = monoMs() + 1200;
    revision_++;
  }

  /// True for about a second after forceSync().
  bool syncing() {
    std::lock_guard<std::mutex> lock(mutex_);
    return monoMs() < syncUntilMs_;
  }

  // ------------------------------------------------------- server states

  void setNtpServerState(const NtpServerState& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    ntpServer_ = s;
  }
  NtpServerState ntpServerState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return ntpServer_;
  }
  void setBleServerState(const BleServerState& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    bleServer_ = s;
  }
  BleServerState bleServerState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bleServer_;
  }
  void setBleClientState(const char* text) {
    std::lock_guard<std::mutex> lock(mutex_);
    copyText(bleClient_, text);
  }
  void bleClientState(char* buf, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    snprintf(buf, len, "%s", bleClient_);
  }
  void setGnssState(const GnssState& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    gnss_ = s;
  }
  GnssState gnssState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return gnss_;
  }

  // -------------------------------------------------------------- output

  /// Runs the fusion once per second; call it often. Returns true if it ran.
  /// force: run it now (for tests).
  bool tick(bool force = false) {
    std::vector<std::pair<Alert, bool>> changes;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const uint64_t now = monoMs();
      if (!force && hasTicked_ && now - lastTickMs_ < 1000) return false;
      hasTicked_ = true;
      lastTickMs_ = now;
      if (regained_) {
        regained_ = false;
        for (Runtime& rt : runtime_) rt.hasPolled = false;
      }
      fuse(now);
      stepClockIfNeeded();
      evaluateAlerts(now, changes);
      revision_++;
    }
    if (onAlert) {
      for (auto& c : changes) onAlert(c.first, c.second);
    }
    return true;
  }

  ClockStatus status() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClockStatus s = status_;
    s.lastDisciplineMs = wallOf(lastDisciplineMono_);
    return s;
  }

  /// The state of the source with the index in config().sources.
  SourceTelemetry telemetry(const char* id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Telemetry& t : telemetry_) {
      if (strcmp(t.id, id) == 0) {
        SourceTelemetry r = t.value;
        if (t.atMono != 0) r.lastSeenMs = wallOf(t.atMono);
        return r;
      }
    }
    SourceTelemetry none;
    copyText(none.detail, "waiting for first sample");
    return none;
  }

  /// The newest `maxPoints` telemetry points, oldest first.
  void history(std::vector<TelemetryPoint>& out, size_t maxPoints) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t n = std::min(maxPoints, history_.size());
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = history_[history_.size() - n + i];
  }

  /// The currently active alerts.
  std::vector<Alert> alerts() {
    std::lock_guard<std::mutex> lock(mutex_);
    return alerts_;
  }

  /// Changes whenever new data is available (for the GUI).
  uint32_t revision() {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
  }

  /// The selected time zone.
  TimeZoneInfo timeZone() {
    std::lock_guard<std::mutex> lock(mutex_);
    return zone_;
  }

  /// The clock that the servers publish (SystemConfig::publishSourceId).
  Published published() {
    std::lock_guard<std::mutex> lock(mutex_);
    const double local = clock_->nowMs();
    const uint64_t now = monoMs();
    Published p;
    const char* id = config_.publishSourceId;
    if (strcmp(id, kPublishSystem) == 0) {
      p.nowMs = local;
      p.stratum = config_.ntpServer.stratum;
      p.rootDispMs = 20;
      p.lastDisciplineMs = local;
      return p;
    }
    const SourceConfig* src = strcmp(id, kPublishFused) == 0 ? nullptr : config_.find(id);
    if (src == nullptr) {
      // fused (or the chosen source was removed)
      p.nowMs = local + fusedOffsetMs_;
      p.stratum = curStratum_;
      p.refSource = strcmp(curRefId_, "GPS") == 0 ? kRefGps : strcmp(curRefId_, "NTP") == 0 ? kRefNtp : kRefUnknown;
      p.rootDelayMs = rootDelayMs_;
      p.rootDispMs = rootDispMs_;
      p.lastDisciplineMs = wallOf(lastDisciplineMono_);
      return p;
    }
    const Runtime* rt = findRuntime(src->id);
    const bool fresh = src->enabled && rt != nullptr && rt->hasSample && !radioOff(*src) &&
                       now - rt->atMono <= freshWindowMs(src->pollIntervalSec);
    if (!fresh) {
      // the chosen source is off or has no recent sample: unsynchronised
      p.nowMs = local + fusedOffsetMs_;
      p.stratum = 16;
      p.lastDisciplineMs = wallOf(lastDisciplineMono_);
      return p;
    }
    const double delay = hasValue(rt->delayMs) ? rt->delayMs : 0.0;
    p.nowMs = local + rt->offsetMs;
    p.stratum = config_.ntpServer.stratum;
    p.refSource = src->type == SourceType::kGnss ? kRefGps : src->type == SourceType::kNtp ? kRefNtp : kRefUnknown;
    p.rootDelayMs = delay;
    p.rootDispMs = jitterOf(*rt) + delay / 2;
    p.lastDisciplineMs = wallOf(rt->atMono);
    return p;
  }

  /// True if a server is enabled.
  bool publishing() {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.ntpServer.enabled || config_.bleCts.enabled;
  }

 protected:
  struct Runtime {
    char id[16] = "";
    bool hasSample = false;
    double offsetMs = 0;
    double delayMs = 0;
    uint64_t atMono = 0;
    uint8_t reach = 0;
    uint64_t lastReachShift = 0;
    bool hasPolled = false;
    uint64_t lastPollMs = 0;
    char error[64] = "";
    char detail[64] = "";
    RingBuffer<double> offsets{TS_JITTER_SAMPLES};
  };

  struct Telemetry {
    char id[16] = "";
    uint64_t atMono = 0;
    SourceTelemetry value;
  };

  struct Cand {
    const SourceConfig* s;
    double offset, delay, jitter;
  };

  SystemClock systemClock_;
  LocalClock* clock_;
  std::mutex mutex_;
  SystemConfig config_ = defaultConfig();
  TimeZoneInfo zone_;
  std::vector<Runtime> runtime_;
  std::vector<Telemetry> telemetry_;
  RingBuffer<TelemetryPoint> history_;
  std::vector<std::pair<uint64_t, double>> fusedTrail_;
  std::vector<Alert> alerts_;
  std::vector<std::pair<std::string, uint64_t>> errorSince_;
  uint64_t unsyncSince_ = 0;

  double fusedOffsetMs_ = 0;
  bool hasLock_ = false;
  int holdoverSec_ = 0;
  uint64_t lastDisciplineMono_ = 0;
  double rootDelayMs_ = 0;
  double rootDispMs_ = 1000;
  int curStratum_ = 16;
  char curRefId_[8] = "INIT";
  ClockStatus status_;
  double steppedMs_ = 0;

  bool network_ = true;
  bool bluetooth_ = true;
  bool regained_ = false;
  NtpServerState ntpServer_;
  BleServerState bleServer_;
  GnssState gnss_;
  char bleClient_[48] = "idle";

  bool hasTicked_ = false;
  uint64_t lastTickMs_ = 0;
  uint64_t syncUntilMs_ = 0;
  uint32_t revision_ = 0;
  // 64 bit millis()
  uint32_t lastMillis_ = 0;
  uint64_t monoBase_ = 1;  // never 0: 0 means "never"

  uint64_t monoMs() {
    const uint32_t m = millis();
    monoBase_ += static_cast<uint32_t>(m - lastMillis_);
    lastMillis_ = m;
    return monoBase_;
  }

  /// The local clock time (ms) of a monotonic time stamp.
  double wallOf(uint64_t mono) {
    if (mono == 0) return 0;
    return clock_->nowMs() - static_cast<double>(monoMs() - mono);
  }

  Runtime& runtimeFor(const char* id) {
    for (Runtime& r : runtime_) {
      if (strcmp(r.id, id) == 0) return r;
    }
    runtime_.emplace_back();
    copyText(runtime_.back().id, id);
    return runtime_.back();
  }

  const Runtime* findRuntime(const char* id) const {
    for (const Runtime& r : runtime_) {
      if (strcmp(r.id, id) == 0) return &r;
    }
    return nullptr;
  }

  /// A source whose transport is switched off can not deliver anything.
  bool radioOff(const SourceConfig& s) const {
    switch (s.type) {
      case SourceType::kNtp:
        return !network_;
      case SourceType::kBleCts:
        return !bluetooth_;
      case SourceType::kGnss:
        return !gnss_.available;
    }
    return false;
  }

  /// A sample counts as current for two missed polls (plus slack).
  static uint64_t freshWindowMs(uint32_t pollSec) {
    return std::max<uint64_t>(2ULL * std::max<uint32_t>(1, pollSec) * 1000ULL + 5000ULL, 10000ULL);
  }

  static double jitterOf(const Runtime& rt) {
    const size_t n = rt.offsets.size();
    if (n < 2) return 0.0;
    double mean = 0;
    for (size_t i = 0; i < n; ++i) mean += rt.offsets[i];
    mean /= n;
    double sq = 0;
    for (size_t i = 0; i < n; ++i) sq += (rt.offsets[i] - mean) * (rt.offsets[i] - mean);
    return sqrt(sq / n);
  }

  void fuse(uint64_t now) {
    std::vector<Telemetry> tel;
    std::vector<Cand> cands;
    const DiscipliningConfig& d = config_.disciplining;

    for (int i = 0; i < config_.sourceCount; ++i) {
      const SourceConfig& s = config_.sources[i];
      Runtime& rt = runtimeFor(s.id);
      Telemetry t;
      copyText(t.id, s.id);
      if (!s.enabled) {
        t.value.status = SourceStatus::kStandby;
        copyText(t.value.detail, "disabled");
        tel.push_back(t);
        continue;
      }
      const uint64_t pollMs = std::max<uint32_t>(1, s.pollIntervalSec) * 1000ULL;
      const bool off = radioOff(s);
      const bool fresh = !off && rt.hasSample && now - rt.atMono <= freshWindowMs(s.pollIntervalSec);
      if (now - rt.lastReachShift >= pollMs) {
        rt.reach = static_cast<uint8_t>((rt.reach << 1) | (fresh ? 1 : 0));
        rt.lastReachShift = now;
      }
      const double jitter = jitterOf(rt);
      if (fresh) cands.push_back({&s, rt.offsetMs, hasValue(rt.delayMs) ? rt.delayMs : 0.0, jitter});
      SourceStatus st;
      if (off) {
        st = SourceStatus::kStandby;
      } else if (!rt.hasSample) {
        st = rt.error[0] ? SourceStatus::kError : SourceStatus::kStandby;
      } else if (!fresh) {
        st = SourceStatus::kError;
      } else {
        st = SourceStatus::kCandidate;
      }
      t.atMono = rt.hasSample ? rt.atMono : 0;
      t.value.offsetMs = rt.hasSample ? rt.offsetMs : 0;
      t.value.delayMs = rt.hasSample ? rt.delayMs : 0;
      t.value.jitterMs = jitter;
      t.value.reach = rt.reach;
      t.value.status = st;
      if (off) {
        copyText(t.value.detail, s.type == SourceType::kNtp      ? "no network"
                                 : s.type == SourceType::kBleCts ? "Bluetooth not available"
                                                                 : "no GNSS receiver connected");
      } else if (rt.error[0]) {
        copyText(t.value.detail, rt.error);
      } else if (rt.detail[0]) {
        copyText(t.value.detail, rt.detail);
      } else if (!rt.hasSample) {
        copyText(t.value.detail, "waiting for first sample");
      }
      tel.push_back(t);
    }

    // outlier rejection (needs >= 3 sources for a meaningful median)
    std::vector<Cand> inliers;
    if (cands.size() >= 3) {
      std::vector<double> sorted;
      for (const Cand& c : cands) sorted.push_back(c.offset);
      std::sort(sorted.begin(), sorted.end());
      const double med = sorted[sorted.size() / 2];
      for (const Cand& c : cands) {
        if (fabs(c.offset - med) <= std::max(50.0, 5 * c.jitter)) inliers.push_back(c);
      }
    } else {
      inliers = cands;
    }
    for (const Cand& c : cands) {
      const bool in = std::any_of(inliers.begin(), inliers.end(), [&](const Cand& x) { return x.s == c.s; });
      for (Telemetry& t : tel) {
        if (strcmp(t.id, c.s->id) == 0) t.value.status = in ? SourceStatus::kLocked : SourceStatus::kOutlier;
      }
    }

    std::vector<Cand> weighted;
    for (const Cand& c : inliers) {
      if (c.s->weight > 0) weighted.push_back(c);
    }
    if (weighted.empty()) weighted = inliers;
    if (!weighted.empty()) {
      double target;
      if (d.algorithm == Algorithm::kMarzullo) {
        std::vector<double> sorted;
        for (const Cand& c : weighted) sorted.push_back(c.offset);
        std::sort(sorted.begin(), sorted.end());
        target = sorted[sorted.size() / 2];
      } else {
        double wSum = 0, acc = 0;
        for (const Cand& c : weighted) {
          const double w = (c.s->weight > 0 ? c.s->weight : 1.0) * (d.preferStratum1 && c.s->stratum <= 1 ? 2.0 : 1.0);
          wSum += w;
          acc += w * c.offset;
        }
        target = acc / wSum;
      }
      if (!hasLock_) {
        fusedOffsetMs_ = target;
        hasLock_ = true;
        fusedTrail_.clear();
      } else {
        const double tau = std::max(1, d.pllTimeConstantSec);
        const double alpha = std::min(1.0, 1.0 / tau + (d.algorithm == Algorithm::kPllFll ? d.fllBandwidthHz : 0.0));
        const double err = target - fusedOffsetMs_;
        const double maxSlew = d.maxSlewPpm * 1e-3;
        fusedOffsetMs_ += fabs(err) > 128.0 ? err : std::max(-maxSlew, std::min(maxSlew, err * alpha));
      }
      holdoverSec_ = 0;
      lastDisciplineMono_ = now;
      rootDelayMs_ = INFINITY;
      double maxJitter = 0;
      for (const Cand& c : inliers) {
        rootDelayMs_ = std::min(rootDelayMs_, c.delay);
        maxJitter = std::max(maxJitter, c.jitter);
      }
      if (!isfinite(rootDelayMs_)) rootDelayMs_ = 0;
      rootDispMs_ = maxJitter + rootDelayMs_ / 2;
      const Cand* best = &weighted[0];
      for (const Cand& c : weighted) {
        if (c.s->stratum < best->s->stratum) best = &c;
      }
      copyText(curRefId_, best->s->type == SourceType::kGnss  ? "GPS"
                          : best->s->type == SourceType::kNtp ? "NTP"
                                                              : "BLE");
    } else if (hasLock_) {
      holdoverSec_++;
      rootDispMs_ += 0.001;  // assume 1 ppm free running error
      copyText(curRefId_, "HOLD");
    }
    curStratum_ = !hasLock_ || holdoverSec_ > d.holdoverTimeoutSec ? 16 : config_.ntpServer.stratum;

    // the drift is measured from the first lock on (the jump to the first
    // offset is not a drift)
    if (hasLock_) fusedTrail_.push_back({now, fusedOffsetMs_});
    while (fusedTrail_.size() > 1 && now - fusedTrail_.front().first > 60000) fusedTrail_.erase(fusedTrail_.begin());
    double drift = 0;
    if (fusedTrail_.size() >= 10) {
      const double dt = (now - fusedTrail_.front().first) / 1000.0;
      if (dt > 0) drift = (fusedOffsetMs_ - fusedTrail_.front().second) / dt * 1e6;
    }

    telemetry_ = tel;
    status_.offsetMs = fusedOffsetMs_;
    status_.driftPpb = drift;
    status_.rootDispersionMs = rootDispMs_;
    status_.rootDelayMs = rootDelayMs_;
    status_.activeStratum = curStratum_;
    copyText(status_.lockedRefId, curRefId_);
    status_.synced = hasLock_ && curStratum_ < 16;
    status_.holdoverSec = holdoverSec_;
    status_.steppedMs = steppedMs_;

    auto avg = [&](SourceType type, bool delay) -> double {
      double sum = 0;
      int n = 0;
      for (const Cand& c : inliers) {
        if (c.s->type != type) continue;
        sum += delay ? c.delay : c.offset;
        n++;
      }
      return n > 0 ? sum / n : NAN;
    };
    TelemetryPoint p;
    p.timestampMs = clock_->nowMs();
    p.fusedOffsetMs = hasLock_ ? fusedOffsetMs_ : NAN;
    p.driftPpb = static_cast<float>(drift);
    p.ntpOffsetMs = avg(SourceType::kNtp, false);
    p.ntpDelayMs = static_cast<float>(avg(SourceType::kNtp, true));
    p.gpsOffsetMs = avg(SourceType::kGnss, false);
    p.bleOffsetMs = avg(SourceType::kBleCts, false);
    p.bleDelayMs = static_cast<float>(avg(SourceType::kBleCts, true));
    history_.push(p);
  }

  /// Applies a large fused offset to the system clock in one step.
  void stepClockIfNeeded() {
    if (!config_.stepSystemClock || !hasLock_ || holdoverSec_ > 0) return;
    const double step = fusedOffsetMs_;
    if (fabs(step) <= TS_STEP_THRESHOLD_MS) return;
    if (!clock_->step(step)) return;
    // everything that was measured against the old clock moves with it
    steppedMs_ += step;
    fusedOffsetMs_ -= step;
    for (Runtime& rt : runtime_) {
      rt.offsetMs -= step;
      for (size_t i = 0; i < rt.offsets.size(); ++i) rt.offsets[i] -= step;
    }
    for (Telemetry& t : telemetry_) t.value.offsetMs -= step;
    // the history too: the charts show no jump
    for (size_t i = 0; i < history_.size(); ++i) {
      TelemetryPoint& h = history_[i];
      h.fusedOffsetMs -= step;
      h.ntpOffsetMs -= step;
      h.gpsOffsetMs -= step;
      h.bleOffsetMs -= step;
    }
    fusedTrail_.clear();  // the drift restarts
    status_.offsetMs = fusedOffsetMs_;
    status_.steppedMs = steppedMs_;
  }

  void setAlert(const char* key, bool active, const char* title, const char* text,
                std::vector<std::pair<Alert, bool>>& changes) {
    for (size_t i = 0; i < alerts_.size(); ++i) {
      if (strcmp(alerts_[i].key, key) != 0) continue;
      if (!active) {
        changes.push_back({alerts_[i], false});
        alerts_.erase(alerts_.begin() + i);
      } else if (strcmp(alerts_[i].text, text) != 0) {
        copyText(alerts_[i].text, text);
        changes.push_back({alerts_[i], true});
      }
      return;
    }
    if (!active) return;
    Alert a;
    copyText(a.key, key);
    copyText(a.title, title);
    copyText(a.text, text);
    alerts_.push_back(a);
    changes.push_back({a, true});
  }

  /// Turns persistent problems into alerts and clears them once they are gone.
  void evaluateAlerts(uint64_t now, std::vector<std::pair<Alert, bool>>& changes) {
    setAlert("ntp_server", config_.ntpServer.enabled && ntpServer_.error[0] != 0, "NTP server failed",
             ntpServer_.error, changes);
    setAlert("ble_server", config_.bleCts.enabled && bleServer_.error[0] != 0, "BLE CTS server failed",
             bleServer_.error, changes);

    // publishing while no source is reachable: clients see stratum 16
    const bool publishing = config_.ntpServer.enabled || config_.bleCts.enabled;
    bool unsync = false;
    if (publishing && curStratum_ >= 16 && network_) {
      if (unsyncSince_ == 0) unsyncSince_ = now;
      unsync = now - unsyncSince_ > 60000;
    } else {
      unsyncSince_ = 0;
    }
    setAlert("unsync", unsync, "Publishing without sync", "No time source is reachable: clients get stratum 16.",
             changes);

    // a source that has been failing for a minute
    for (int i = 0; i < config_.sourceCount; ++i) {
      const SourceConfig& s = config_.sources[i];
      char key[24];
      snprintf(key, sizeof(key), "src_%s", s.id);
      const Telemetry* t = nullptr;
      for (const Telemetry& x : telemetry_) {
        if (strcmp(x.id, s.id) == 0) t = &x;
      }
      bool failing = false;
      auto it = std::find_if(errorSince_.begin(), errorSince_.end(),
                             [&](const std::pair<std::string, uint64_t>& e) { return e.first == s.id; });
      if (s.enabled && t != nullptr && t->value.status == SourceStatus::kError && !radioOff(s)) {
        if (it == errorSince_.end()) {
          errorSince_.push_back({s.id, now});
        } else {
          failing = now - it->second > 60000;
        }
      } else if (it != errorSince_.end()) {
        errorSince_.erase(it);
      }
      char title[40];
      snprintf(title, sizeof(title), "%s failing", s.name);
      setAlert(key, failing, title, t != nullptr ? t->value.detail : "-", changes);
    }
  }
};

}  // namespace time_services

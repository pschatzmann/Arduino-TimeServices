#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "TimeServices/TimeServicesConfig.h"

namespace time_services {

/// Copies `src` into the fixed size buffer `dst` (always terminated).
template <size_t N>
inline void copyText(char (&dst)[N], const char* src) {
  snprintf(dst, N, "%s", src != nullptr ? src : "");
}

/// True if `v` holds a real value (not NaN / inf).
inline bool hasValue(double v) { return isfinite(v); }

enum class SourceType : uint8_t { kNtp = 0, kGnss = 1, kBleCts = 2 };

/// One time source. The structs of the configuration only contain fixed
/// size fields, so the whole SystemConfig can be stored as bytes (e.g. with
/// Preferences::putBytes()).
struct SourceConfig {
  char id[16] = "";
  char name[32] = "";
  SourceType type = SourceType::kNtp;
  bool enabled = true;
  uint8_t stratum = 2;
  /// Share of the fused offset (0..1); 0 = only used if no other source.
  float weight = 0.25f;
  uint32_t pollIntervalSec = 16;
  /// NTP: host or host:port. BLE CTS: address or name of the peer (empty:
  /// the first device with the Current Time Service).
  char host[64] = "";

  static SourceConfig make(const char* id, const char* name, SourceType type, bool enabled, int stratum,
                           float weight, uint32_t poll, const char* host = "") {
    SourceConfig s;
    copyText(s.id, id);
    copyText(s.name, name);
    s.type = type;
    s.enabled = enabled;
    s.stratum = static_cast<uint8_t>(stratum);
    s.weight = weight;
    s.pollIntervalSec = poll;
    copyText(s.host, host);
    return s;
  }
};

/// How the readings of the sources are combined.
enum class Algorithm : uint8_t {
  /// Weighted mean of the inliers, followed by the loop filter.
  kKalman = 0,
  /// Median of the inliers.
  kMarzullo = 1,
  /// Weighted mean with a faster loop (time constant + FLL bandwidth).
  kPllFll = 2
};

struct DiscipliningConfig {
  Algorithm algorithm = Algorithm::kKalman;
  int pllTimeConstantSec = 16;
  float fllBandwidthHz = 0.05f;
  int maxSlewPpm = 500;
  int holdoverTimeoutSec = 86400;
  bool preferStratum1 = true;
};

/// The NTP server this device offers (TinyNTPServer).
struct NtpServerConfig {
  bool enabled = false;
  uint16_t listenPort = 123;
  uint8_t stratum = 2;
  /// Requests per minute and client; 0 = unlimited.
  uint16_t rateLimitPerMin = 120;
  /// Comma separated IPv4 subnets (CIDR) that are answered.
  char allowSubnets[64] = "0.0.0.0/0";
  /// Reference id in the answers (up to 4 characters).
  char refId[5] = "CHRN";
  /// Leap indicator in the answers: 0 none, 1 +1 s, 2 -1 s, 3 unsynchronised.
  uint8_t leapIndicator = 0;
};

/// The BLE Current Time Service this device offers (TinyCTSServer).
struct BleCtsConfig {
  bool enabled = false;
  /// Advertising interval in ms.
  uint16_t advIntervalMs = 1000;
  bool sendNotifications = true;
  /// Interval of the 0x2A2B notifications, 100 ms .. 24 h.
  uint32_t notifyIntervalMs = 1000;
};

/// What the NTP and BLE servers publish.
constexpr const char* kPublishFused = "fused";
constexpr const char* kPublishSystem = "system";

/// All settings.
struct SystemConfig {
  DiscipliningConfig disciplining;
  NtpServerConfig ntpServer;
  BleCtsConfig bleCts;
  SourceConfig sources[TS_MAX_SOURCES];
  uint8_t sourceCount = 0;
  /// "fused", "system" or the id of one source.
  char publishSourceId[16] = "fused";
  /// IANA name of the time zone (see kZones in TimeZoneInfo.h).
  char timeZone[32] = "UTC";
  /// Step the system clock (settimeofday()) to the fused time, so that
  /// time() / gettimeofday() of the device are correct as well.
  bool stepSystemClock = false;

  SourceConfig* find(const char* id) {
    for (int i = 0; i < sourceCount; ++i) {
      if (strcmp(sources[i].id, id) == 0) return &sources[i];
    }
    return nullptr;
  }
  const SourceConfig* find(const char* id) const { return const_cast<SystemConfig*>(this)->find(id); }

  /// The first source of the indicated type, or nullptr.
  const SourceConfig* firstOfType(SourceType type, bool enabledOnly = false) const {
    for (int i = 0; i < sourceCount; ++i) {
      if (sources[i].type == type && (!enabledOnly || sources[i].enabled)) return &sources[i];
    }
    return nullptr;
  }

  bool addSource(const SourceConfig& s) {
    if (sourceCount >= TS_MAX_SOURCES) return false;
    sources[sourceCount++] = s;
    return true;
  }

  void removeSource(int index) {
    if (index < 0 || index >= sourceCount) return;
    for (int i = index; i < sourceCount - 1; ++i) sources[i] = sources[i + 1];
    sourceCount--;
    sources[sourceCount] = SourceConfig();
  }
};

/// The default configuration (the same sources as the Android app).
inline SystemConfig defaultConfig() {
  SystemConfig c;
  c.addSource(SourceConfig::make("src_gps", "GNSS receiver", SourceType::kGnss, true, 1, 0.10f, 1));
  c.addSource(
      SourceConfig::make("src_ntp_cf", "Cloudflare NTP", SourceType::kNtp, true, 1, 0.35f, 16, "time.cloudflare.com"));
  c.addSource(
      SourceConfig::make("src_ntp_google", "Google Public NTP", SourceType::kNtp, true, 1, 0.35f, 16, "time.google.com"));
  c.addSource(SourceConfig::make("src_ble_cts", "BLE CTS peripheral", SourceType::kBleCts, false, 2, 0.05f, 60));
  return c;
}

enum class SourceStatus : uint8_t { kLocked, kCandidate, kOutlier, kStandby, kError };

inline const char* statusLabel(SourceStatus s) {
  switch (s) {
    case SourceStatus::kLocked:
      return "locked";
    case SourceStatus::kCandidate:
      return "candidate";
    case SourceStatus::kOutlier:
      return "outlier";
    case SourceStatus::kStandby:
      return "standby";
    case SourceStatus::kError:
      return "error";
  }
  return "";
}

/// The measured state of one source.
struct SourceTelemetry {
  /// Local (system clock) time of the last sample in ms, 0 = never.
  double lastSeenMs = 0;
  double offsetMs = 0;
  /// Round trip; NaN if the source does not report it.
  double delayMs = 0;
  double jitterMs = 0;
  uint8_t reach = 0;
  SourceStatus status = SourceStatus::kStandby;
  char detail[64] = "";
};

/// The state of the fused clock.
struct ClockStatus {
  double offsetMs = 0;
  double driftPpb = 0;
  double rootDispersionMs = 0;
  double rootDelayMs = 0;
  int activeStratum = 16;
  char lockedRefId[8] = "INIT";
  double lastDisciplineMs = 0;
  bool synced = false;
  int holdoverSec = 0;
  /// Sum of the steps that were applied to the system clock.
  double steppedMs = 0;
};

/// One point of the Metrics charts (one per second). Missing values are NaN.
/// The offsets are double: before the clock of a board is set they are about
/// 1.8e12 ms, where a float would only resolve ~100 s.
struct TelemetryPoint {
  double timestampMs = 0;
  double fusedOffsetMs = NAN;
  float driftPpb = NAN;
  double ntpOffsetMs = NAN;
  float ntpDelayMs = NAN;
  double gpsOffsetMs = NAN;
  double bleOffsetMs = NAN;
  float bleDelayMs = NAN;
};

struct NtpServerState {
  bool running = false;
  uint16_t port = 0;
  uint32_t packetsServed = 0;
  uint32_t dropped = 0;
  char lastClient[16] = "";
  char error[48] = "";
  /// Enabled, but stopped because there is no network.
  bool waitingForNetwork = false;
};

struct BleServerState {
  bool running = false;
  /// Connected peers; -1 if the Bluetooth stack does not report it.
  int connected = -1;
  char lastPacketHex[32] = "";
  char error[48] = "";
  /// Enabled, but stopped because Bluetooth is not available.
  bool waitingForBluetooth = false;
};

struct GnssState {
  /// A receiver is connected (GnssSource::begin() was called).
  bool available = false;
  bool hasFix = false;
  int satsUsed = 0;
  int satsVisible = 0;
  /// A PPS pin is configured / pulses arrive.
  bool ppsConfigured = false;
  bool ppsActive = false;
};

/// A problem that persists (port of the Android notifications).
struct Alert {
  char key[24] = "";
  char title[40] = "";
  char text[64] = "";
};

/// The clock a server publishes, see SystemConfig::publishSourceId.
struct Published {
  /// UTC time in ms since 1970.
  double nowMs = 0;
  int stratum = 16;
  /// Reference time source of the BLE CTS (0x2A14): 0 unknown, 1 NTP, 2 GPS.
  uint8_t refSource = 0;
  double rootDelayMs = 0;
  double rootDispMs = 1000;
  double lastDisciplineMs = 0;
};

/// Reference time source codes of the BLE Reference Time Information.
constexpr uint8_t kRefUnknown = 0;
constexpr uint8_t kRefNtp = 1;
constexpr uint8_t kRefGps = 2;

/// host or host:port, no spaces; port 1..65535.
inline bool isValidNtpHost(const char* text) {
  if (text == nullptr || *text == 0) return false;
  int colons = 0;
  const char* colon = nullptr;
  for (const char* p = text; *p; ++p) {
    const char c = *p;
    if (c == ':') {
      colons++;
      colon = p;
      continue;
    }
    if (colon != nullptr) {
      if (c < '0' || c > '9') return false;
    } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                 c == '-')) {
      return false;
    }
  }
  if (colons > 1) return false;
  if (colon != nullptr) {
    if (colon == text) return false;
    const long port = atol(colon + 1);
    if (colon[1] == 0 || port < 1 || port > 65535) return false;
  }
  return true;
}

/// Splits host:port; the port is 123 if missing.
inline void splitHostPort(const char* text, char* host, size_t hostLen, int& port) {
  port = 123;
  snprintf(host, hostLen, "%s", text != nullptr ? text : "");
  char* colon = strchr(host, ':');
  if (colon != nullptr && strchr(colon + 1, ':') == nullptr) {
    *colon = 0;
    const int p = atoi(colon + 1);
    if (p > 0 && p < 65536) port = p;
  }
}

/// Compact interval, e.g. 1500 -> "1.5 s", 120000 -> "2 min".
inline const char* formatInterval(char* buf, size_t len, uint32_t ms) {
  if (ms % 3600000 == 0) {
    snprintf(buf, len, "%lu h", static_cast<unsigned long>(ms / 3600000));
  } else if (ms % 60000 == 0) {
    snprintf(buf, len, "%lu min", static_cast<unsigned long>(ms / 60000));
  } else if (ms % 1000 == 0) {
    snprintf(buf, len, "%lu s", static_cast<unsigned long>(ms / 1000));
  } else if (ms > 1000) {
    snprintf(buf, len, "%.1f s", ms / 1000.0);
  } else {
    snprintf(buf, len, "%lu ms", static_cast<unsigned long>(ms));
  }
  return buf;
}

}  // namespace time_services

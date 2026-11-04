// Tests of the time engine (fusion, outliers, holdover, publishing, clock
// steps) and the time zone details. The NMEA parser and the access control of
// the NTP server are tested in TinyNTP.
#include <math.h>
#include <stdio.h>

#include <vector>

#include "TimeServices/TimeEngine.h"
#include "TimeServices/TimeZoneInfo.h"

using namespace time_services;

static int failures = 0;

static void check(const char* name, bool ok) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}

static void checkNear(const char* name, double expected, double actual, double tolerance) {
  const bool ok = isfinite(actual) && fabs(expected - actual) <= tolerance;
  printf("%s %-50s expected %12.3f +- %-6.2f got %12.3f\n", ok ? "PASS" : "FAIL", name, expected, tolerance, actual);
  if (!ok) failures++;
}

/// A clock that stands still: the offsets are exactly what is reported.
class FixedClock : public LocalClock {
 public:
  double now = 1793793600000.0;  // 2026-11-04 12:00 UTC
  bool allowStep = true;
  double nowMs() override { return now; }
  bool step(double deltaMs) override {
    if (!allowStep) return false;
    now += deltaMs;
    return true;
  }
};

static SystemConfig threeNtp() {
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  c.addSource(SourceConfig::make("b", "B", SourceType::kNtp, true, 2, 0.3f, 16, "b.example"));
  c.addSource(SourceConfig::make("c", "C", SourceType::kNtp, true, 2, 0.3f, 16, "c.example"));
  return c;
}

static void testFirstLock() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  engine.begin(c);
  check("not synced before the first sample", !engine.status().synced);
  engine.recordSample("a", 25.0, 10.0);
  engine.tick(true);
  checkNear("first sample is taken over", 25.0, engine.status().offsetMs, 1e-9);
  check("synced after the first sample", engine.status().synced);
  check("reference is NTP", strcmp(engine.status().lockedRefId, "NTP") == 0);
  checkNear("root delay", 10.0, engine.status().rootDelayMs, 1e-9);
  check("source is locked", engine.telemetry("a").status == SourceStatus::kLocked);
  checkNear("nowMs = local + offset", clock.now + 25.0, engine.nowMs(), 1e-6);
}

static void testSlew() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  c.disciplining.pllTimeConstantSec = 10;
  c.disciplining.maxSlewPpm = 500;  // 0.5 ms per tick
  engine.begin(c);
  engine.recordSample("a", 0.0, 1.0);
  engine.tick(true);
  engine.recordSample("a", 2.0, 1.0);
  engine.tick(true);
  // error 2 ms * alpha 0.1 = 0.2 ms
  checkNear("slewed with the loop filter", 0.2, engine.status().offsetMs, 1e-9);
  engine.recordSample("a", 100.0, 1.0);
  engine.tick(true);
  checkNear("slew limited to maxSlewPpm", 0.7, engine.status().offsetMs, 1e-9);
  engine.recordSample("a", 1000.0, 1.0);
  engine.tick(true);
  checkNear("large error is stepped", 1000.0, engine.status().offsetMs, 1e-9);
}

static void testOutlier() {
  FixedClock clock;
  TimeEngine engine(clock);
  engine.begin(threeNtp());
  engine.recordSample("a", 10.0, 5.0);
  engine.recordSample("b", 12.0, 6.0);
  engine.recordSample("c", 500.0, 7.0);
  engine.tick(true);
  check("outlier detected", engine.telemetry("c").status == SourceStatus::kOutlier);
  check("inlier locked", engine.telemetry("a").status == SourceStatus::kLocked);
  checkNear("fused offset without the outlier", 11.0, engine.status().offsetMs, 1e-9);
}

static void testMedian() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c = threeNtp();
  c.disciplining.algorithm = Algorithm::kMarzullo;
  engine.begin(c);
  engine.recordSample("a", 10.0, 5.0);
  engine.recordSample("b", 20.0, 6.0);
  engine.recordSample("c", 40.0, 7.0);
  engine.tick(true);
  checkNear("median of the sources", 20.0, engine.status().offsetMs, 1e-9);
}

static void testWeightsAndStratum() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("gps", "GPS", SourceType::kGnss, true, 1, 0.5f, 1));
  c.addSource(SourceConfig::make("ntp", "NTP", SourceType::kNtp, true, 2, 0.5f, 16, "x"));
  engine.begin(c);
  GnssState gs;
  gs.available = true;
  engine.setGnssState(gs);
  engine.recordSample("gps", 0.0, NAN);
  engine.recordSample("ntp", 30.0, 2.0);
  engine.tick(true);
  // stratum 1 counts twice: (2*0.5*0 + 0.5*30) / 1.5 = 10
  checkNear("stratum 1 preferred", 10.0, engine.status().offsetMs, 1e-9);
  check("reference is GPS", strcmp(engine.status().lockedRefId, "GPS") == 0);
}

static void testHoldoverAndPublished() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  c.ntpServer.stratum = 3;
  engine.begin(c);
  Published p = engine.published();
  check("unsynchronised: stratum 16", p.stratum == 16);
  engine.recordSample("a", 5.0, 4.0);
  engine.tick(true);
  p = engine.published();
  check("synchronised: stratum of the config", p.stratum == 3);
  checkNear("published time", clock.now + 5.0, p.nowMs, 1e-6);
  check("reference source NTP", p.refSource == kRefNtp);

  // the network goes away: holdover
  engine.setNetworkAvailable(false);
  engine.tick(true);
  check("holdover", engine.status().holdoverSec == 1 && strcmp(engine.status().lockedRefId, "HOLD") == 0);
  check("still synced in holdover", engine.status().synced);
  check("source standby without network", engine.telemetry("a").status == SourceStatus::kStandby);

  copyText(c.publishSourceId, "a");
  engine.setConfig(c);
  check("chosen source without network: stratum 16", engine.published().stratum == 16);
  copyText(c.publishSourceId, kPublishSystem);
  engine.setConfig(c);
  checkNear("system clock published", clock.now, engine.published().nowMs, 1e-6);
}

static void testIsDue() {
  FixedClock clock;
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  c.addSource(SourceConfig::make("b", "B", SourceType::kNtp, false, 2, 0.3f, 16, "b.example"));
  engine.begin(c);
  check("first poll is due", engine.isDue("a"));
  check("second poll is not due", !engine.isDue("a"));
  check("disabled source is never due", !engine.isDue("b"));
  engine.forceSync();
  check("due after forceSync()", engine.isDue("a"));
}

static void testStep() {
  FixedClock clock;
  clock.now = 1000.0;  // the board starts in 1970
  TimeEngine engine(clock);
  SystemConfig c;
  c.addSource(SourceConfig::make("a", "A", SourceType::kNtp, true, 2, 0.3f, 16, "a.example"));
  c.stepSystemClock = true;
  engine.begin(c);
  const double offset = 1793793600000.0 - 1000.0;
  engine.recordSample("a", offset, 4.0);
  engine.tick(true);
  checkNear("clock was stepped", 1793793600000.0, clock.now, 1e-3);
  checkNear("offset after the step", 0.0, engine.status().offsetMs, 1e-3);
  checkNear("steps are summed", offset, engine.status().steppedMs, 1e-3);
  checkNear("source offset moved with the clock", 0.0, engine.telemetry("a").offsetMs, 1e-3);
  engine.tick(true);
  checkNear("no further step", 0.0, engine.status().offsetMs, 1e-3);
  std::vector<TelemetryPoint> history;
  engine.history(history, 10);
  check("history has the points before and after the step", history.size() == 2);
  if (history.size() == 2) {
    checkNear("history before the step moved with the clock", 0.0, history[0].fusedOffsetMs, 1e-3);
    checkNear("history after the step", 0.0, history[1].fusedOffsetMs, 1e-3);
  }
}

static void testTimeZone() {
  TimeZoneInfo z;
  z.select("Europe/Zurich");
  TimeZoneDetails d = z.details(1780272000);  // 2026-06-01
  check("summer time in June", d.isDst && d.offsetMinutes == 120 && d.dstOffsetCode == 4);
  check("formatted offset", strcmp(d.offsetFormatted, "UTC+02:00") == 0);
  check("CTS quarter hours", d.quarterHours == 4);
  check("next: back in October", d.hasNextTransition && d.nextGapHours == -1.0 && strcmp(d.nextDate, "2026-10-25") == 0);
  int64_t at;
  int32_t before, after;
  z.nextTransition(1780272000, at, before, after);
  check("end of summer time (UTC)", at == 1792890000);
  d = z.details(1793793600);  // 2026-11-04
  check("standard time in November", !d.isDst && d.offsetMinutes == 60 && strcmp(d.shortName, "CET") == 0);
  z.nextTransition(1793793600, at, before, after);
  check("start of summer time 2027 (UTC)", at == 1806195600 && after - before == 3600);
  z.select("Asia/Kolkata");
  d = z.details(1793793600);
  check("India +05:30 without summer time", d.offsetMinutes == 330 && !d.hasNextTransition);
  z.select("Unknown/Zone");
  check("unknown zone is UTC", strcmp(z.iana(), "UTC") == 0);
  z.select("America/Sao_Paulo");  // not in the GUI list, but in TinyNTP's table
  check("any IANA zone", strcmp(z.iana(), "America/Sao_Paulo") == 0 && z.details(1793793600).offsetMinutes == -180);
  check("label of a listed zone", (z.select("Europe/Zurich"), strcmp(z.label(), "Zurich") == 0));
}

static void testHosts() {
  check("host", isValidNtpHost("time.google.com"));
  check("host:port", isValidNtpHost("192.168.1.10:12123"));
  check("empty", !isValidNtpHost(""));
  check("space", !isValidNtpHost("time google"));
  check("bad port", !isValidNtpHost("host:99999"));
  check("two colons", !isValidNtpHost("a:1:2"));
  char host[64];
  int port;
  splitHostPort("pool.ntp.org:1234", host, sizeof(host), port);
  check("split host:port", strcmp(host, "pool.ntp.org") == 0 && port == 1234);
  splitHostPort("pool.ntp.org", host, sizeof(host), port);
  check("default port", port == 123);
}

int main() {
  testFirstLock();
  testSlew();
  testOutlier();
  testMedian();
  testWeightsAndStratum();
  testHoldoverAndPublished();
  testIsDue();
  testStep();
  testTimeZone();
  testHosts();
  printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}

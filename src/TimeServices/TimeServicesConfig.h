#pragma once

/// Maximum number of time sources (NTP servers, GNSS, BLE CTS) in the
/// configuration.
#ifndef TS_MAX_SOURCES
#define TS_MAX_SOURCES 8
#endif

/// Number of telemetry points (one per second) kept for the Metrics charts.
#ifndef TS_MAX_HISTORY
#define TS_MAX_HISTORY 900
#endif

/// Number of recent offsets per source used for the jitter.
#ifndef TS_JITTER_SAMPLES
#define TS_JITTER_SAMPLES 8
#endif

/// The fused offset is applied to the system clock in one step when it is
/// larger than this (and SystemConfig::stepSystemClock is set). Smaller
/// offsets are only slewed in the fused estimate.
#ifndef TS_STEP_THRESHOLD_MS
#define TS_STEP_THRESHOLD_MS 128.0
#endif

namespace time_services {
/// TAI - UTC and GPS - UTC in seconds (as in the Android app).
constexpr int kLeapSecondsTai = 37;
constexpr int kLeapSecondsGps = 18;
}  // namespace time_services

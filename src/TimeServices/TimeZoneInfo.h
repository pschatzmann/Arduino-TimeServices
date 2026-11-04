#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "TinyNTP.h"
#include "TinyNTP/TinyTimeZoneTable.h"

namespace time_services {

/// A time zone that can be selected: IANA name, short label for the GUI and
/// the POSIX TZ string (TZ_* defines of TinyNTP).
struct ZoneEntry {
  const char* iana;
  const char* label;
  const char* posix;
};

/// The zones offered in the Settings (the same list as the Android app).
inline const ZoneEntry kZones[] = {
    {"UTC", "UTC", TZ_ETC_UTC},
    {"America/New_York", "New York", TZ_AMERICA_NEW_YORK},
    {"America/Chicago", "Chicago", TZ_AMERICA_CHICAGO},
    {"America/Denver", "Denver", TZ_AMERICA_DENVER},
    {"America/Los_Angeles", "Los Angeles", TZ_AMERICA_LOS_ANGELES},
    {"America/Anchorage", "Anchorage", TZ_AMERICA_ANCHORAGE},
    {"Pacific/Honolulu", "Honolulu", TZ_PACIFIC_HONOLULU},
    {"Europe/London", "London", TZ_EUROPE_LONDON},
    {"Europe/Berlin", "Berlin", TZ_EUROPE_BERLIN},
    {"Europe/Paris", "Paris", TZ_EUROPE_PARIS},
    {"Europe/Zurich", "Zurich", TZ_EUROPE_ZURICH},
    {"Europe/Helsinki", "Helsinki", TZ_EUROPE_HELSINKI},
    {"Asia/Dubai", "Dubai", TZ_ASIA_DUBAI},
    {"Asia/Kolkata", "Kolkata", TZ_ASIA_KOLKATA},
    {"Asia/Singapore", "Singapore", TZ_ASIA_SINGAPORE},
    {"Asia/Tokyo", "Tokyo", TZ_ASIA_TOKYO},
    {"Australia/Sydney", "Sydney", TZ_AUSTRALIA_SYDNEY},
    {"Pacific/Auckland", "Auckland", TZ_PACIFIC_AUCKLAND},
};
constexpr int kZoneCount = sizeof(kZones) / sizeof(kZones[0]);

/// The zone of the GUI list with the IANA name, or nullptr.
inline const ZoneEntry* findListedZone(const char* iana) {
  for (const ZoneEntry& z : kZones) {
    if (strcmp(z.iana, iana) == 0) return &z;
  }
  return nullptr;
}

/// What the Clock page shows about a time zone at one instant.
struct TimeZoneDetails {
  const char* iana = "UTC";
  char shortName[8] = "UTC";
  int offsetMinutes = 0;
  char offsetFormatted[16] = "UTC+00:00";
  /// Standard offset in 15 min units (BLE CTS Local Time Information).
  int quarterHours = 0;
  bool isDst = false;
  /// Summer time offset in minutes now (0, 30, 60, 120).
  int dstMinutes = 0;
  /// 0=standard, 2=+30 min, 4=+1 h, 8=+2 h (BLE CTS DST Offset).
  int dstOffsetCode = 0;
  bool hasNextTransition = false;
  double nextGapHours = 0;
  /// Local date before the transition, "YYYY-MM-DD".
  char nextDate[40] = "";
};

/**
 * @brief The selected time zone: TinyNTP's POSIX TZ parser (TinyTimeZone)
 * plus the IANA name. Any IANA zone can be selected (TinyTimeZoneTable);
 * unknown names select UTC.
 */
class TimeZoneInfo : public tiny_ntp::TinyTimeZone {
 public:
  TimeZoneInfo() { select("UTC"); }

  /// Selects the zone by its IANA name.
  void select(const char* iana) {
    const char* tz = tiny_ntp::findTimeZone(iana);
    if (tz == nullptr) {
      iana = "UTC";
      tz = TZ_ETC_UTC;
    }
    snprintf(iana_, sizeof(iana_), "%s", iana);
    const ZoneEntry* listed = findListedZone(iana_);
    snprintf(label_, sizeof(label_), "%s", listed != nullptr ? listed->label : iana_);
    setTimeZone(tz);
  }

  /// IANA name, e.g. "Europe/Zurich".
  const char* iana() const { return iana_; }
  /// Short name for the GUI, e.g. "Zurich".
  const char* label() const { return label_; }

  TimeZoneDetails details(int64_t utcSec) const {
    TimeZoneDetails d;
    d.iana = iana_;
    const bool dst = isDst(utcSec);
    snprintf(d.shortName, sizeof(d.shortName), "%s", name(utcSec));
    d.offsetMinutes = offsetSeconds(utcSec) / 60;
    const int abs = d.offsetMinutes < 0 ? -d.offsetMinutes : d.offsetMinutes;
    snprintf(d.offsetFormatted, sizeof(d.offsetFormatted), "UTC%c%02d:%02d", d.offsetMinutes >= 0 ? '+' : '-',
             (abs / 60) % 100, abs % 60);
    d.quarterHours = standardOffsetSeconds() / 900;
    d.isDst = dst;
    d.dstMinutes = dstMinutes(utcSec);
    d.dstOffsetCode = d.dstMinutes >= 120 ? 8 : d.dstMinutes >= 60 ? 4 : d.dstMinutes >= 30 ? 2 : 0;
    int64_t next = 0;
    int32_t before = 0, after = 0;
    if (nextTransition(utcSec, next, before, after)) {
      d.hasNextTransition = true;
      d.nextGapHours = (after - before) / 3600.0;
      // the local date before the change
      const std::tm tm = toLocal(next - 1);
      snprintf(d.nextDate, sizeof(d.nextDate), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
    return d;
  }

  /// Local time of a UTC instant as broken down fields.
  std::tm local(int64_t utcSec) const { return toLocal(utcSec); }

 private:
  char iana_[40] = "UTC";
  char label_[40] = "UTC";
};

}  // namespace time_services

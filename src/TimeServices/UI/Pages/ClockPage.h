#pragma once
#include <math.h>
#include <time.h>

#include <string>

#include "TimeServices/TimeZoneInfo.h"
#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ServicePage.h"

namespace time_services {

/**
 * @brief Clock tab: the disciplined time (local, UTC, TAI or GPS), the time
 * zone with its BLE CTS encoding and the quality of the synchronisation.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class ClockPage : public ServicePage<RGB_T> {
  using Base = ServicePage<RGB_T>;
  using typename Base::Placer;
  using Base::kMargin;

 public:
  explicit ClockPage(PageContext<RGB_T>& ctx) : Base(ctx, "Clock", drawClockIcon<RGB_T>) {}


  /// Update interval of the displayed time.
  uint32_t intervalMs = 100;

  void build() override {
    const int32_t w = this->inner();
    Placer p = this->pagePlacer(8);
    this->add(chip_);
    p.place(chip_, 20);
    date_ = PageLabel<RGB_T>("", tinymd::TypographyRole::kLabel, tinymd::TextAlign::kCenter);
    this->add(date_);
    p.place(date_, 12, true, 4);
    time_ = PageLabel<RGB_T>("--:--:--", tinymd::TypographyRole::kHeadline, tinymd::TextAlign::kCenter);
    this->add(time_);
    p.place(time_, 26, true, 2);
    ms_ = PageLabel<RGB_T>(".000", tinymd::TypographyRole::kTitle, tinymd::TextAlign::kCenter);
    ms_.setColor(this->accents().cyan);
    this->add(ms_);
    p.place(ms_, 14, true, 4);
    zoneLine_ = PageLabel<RGB_T>("", tinymd::TypographyRole::kLabel, tinymd::TextAlign::kCenter);
    this->add(zoneLine_);
    p.place(zoneLine_, 12, true, 6);
    scale_ = tinymd::SegmentedButton<RGB_T>(tinymd::Bounds());
    for (const char* s : {"Local", "UTC", "TAI", "GPS"}) scale_.addSegment(s);
    scale_.setSelected(0, true);
    scale_.onChange = [this](uint32_t mask) {
      for (int i = 0; i < 4; ++i) {
        if (mask & (1u << i)) scaleIndex_ = i;
      }
      lastTickMs_ = 0;
    };
    this->add(scale_);
    p.place(scale_, 28, true, 10);

    // time zone card
    this->add(tzFrame_);
    const int32_t tzTop = p.y;
    Placer c = this->cardPlacer(p.y);
    tzCaption_ = PageLabel<RGB_T>("LOCAL TIME ZONE", tinymd::TypographyRole::kLabel);
    tzName_ = PageLabel<RGB_T>("", tinymd::TypographyRole::kBody);
    tzCaption_.setOnSurface(true);
    tzName_.setOnSurface(true);
    this->add(tzCaption_);
    c.place(tzCaption_, 10, true, 2);
    this->add(tzName_);
    c.place(tzName_, 14, true, 6);
    static const char* const captions[4] = {"IANA", "UTC OFFSET", "DST", "BLE CTS 0x2A0F"};
    const int32_t tileW = (c.w - 6) / 2;
    for (int i = 0; i < 4; ++i) {
      tzTiles_[i] = tinymd::StatTile<RGB_T>(captions[i]);
      tzTiles_[i].tonal = true;
      tzTiles_[i].bounds = tinymd::Bounds(c.x + (i % 2) * (tileW + 6), c.y + (i / 2) * 46, tileW, 40);
      this->add(tzTiles_[i]);
    }
    c.y += 2 * 46;
    tzNext_.setOnSurface(true);
    this->add(tzNext_);
    c.place(tzNext_, 24, true, 0);
    tzFrame_.bounds = tinymd::Bounds(kMargin, tzTop, w, c.y - tzTop + 8);
    p.y = tzFrame_.bounds.bottom() + 10;

    // synchronisation quality card
    syncCaption_ = PageLabel<RGB_T>("SYNCHRONISATION QUALITY", tinymd::TypographyRole::kLabel);
    this->add(syncCaption_);
    p.place(syncCaption_, 10);
    this->add(syncFrame_);
    const int32_t syncTop = p.y;
    c = this->cardPlacer(p.y);
    const Accents<RGB_T>& a = this->accents();
    kpis_[0] = KpiRow<RGB_T>("OFFSET VS LOCAL CLOCK",
                             "How far the local clock is from the estimated true time. Negative means the local "
                             "clock is ahead.",
                             a.emerald);
    kpis_[1] = KpiRow<RGB_T>(
        "DRIFT", "How fast that offset is changing, in ppb (1 ppb is 1 ns per second). Near 0 means steady.", a.cyan);
    kpis_[2] = KpiRow<RGB_T>("ROOT DISPERSION",
                             "The error bar of the time estimate, from the spread and delay of the sources. Smaller "
                             "means more trustworthy.",
                             a.amber);
    kpis_[3] = KpiRow<RGB_T>(
        "ROOT DELAY", "Network round trip to the time source. It limits how precisely the time can be known.", a.purple);
    for (auto& kpi : kpis_) {
      this->add(kpi);
      c.place(kpi, kpi.heightFor(c.w), true, 8);
    }
    syncFrame_.bounds = tinymd::Bounds(kMargin, syncTop, w, c.y - syncTop);
    notesY_ = syncFrame_.bounds.bottom() + 8;

    alertText_.setColor(a.rose);
    this->add(alertText_);
    clockNote_.setText(
        "The displayed time is the local clock plus the fused offset estimate of the enabled sources. With \"Step "
        "system clock\" (Settings) the clock of the device is set as well.");
    this->add(clockNote_);
    layoutNotes();
  }

  bool tick(uint32_t nowMs) override {
    if (nowMs - lastTickMs_ < intervalMs) return false;
    lastTickMs_ = nowMs;
    refreshTime();
    return true;
  }

  void onShow() override { lastTickMs_ = 0; }

  void refresh() override {
    TimeEngine& engine = this->engine();
    const Accents<RGB_T>& a = this->accents();
    const ClockStatus st = engine.status();
    char buf[96];
    if (st.synced) {
      snprintf(buf, sizeof(buf), "STRATUM %d - REF %s", st.activeStratum, st.lockedRefId);
    } else {
      snprintf(buf, sizeof(buf), "NOT SYNCHRONISED");
    }
    chip_.setText(buf);
    chip_.setColor(st.synced ? a.emerald : a.amber);

    const TimeZoneInfo zone = engine.timeZone();
    const TimeZoneDetails d = zone.details(static_cast<int64_t>(engine.nowMs() / 1000.0));
    snprintf(buf, sizeof(buf), "%s (%s)", zone.label(), d.shortName);
    tzName_.setText(buf);
    tzTiles_[0].setValue(d.iana);
    tzTiles_[0].setAccent(this->theme().colors.onSurface);
    tzTiles_[1].setValue(d.offsetFormatted);
    tzTiles_[1].setAccent(a.cyan);
    tzTiles_[2].setValue(d.isDst ? "Summer time" : "Standard");
    tzTiles_[2].setAccent(d.isDst ? a.amber : a.emerald);
    snprintf(buf, sizeof(buf), "%dx15' DST %d", d.quarterHours, d.dstOffsetCode);
    tzTiles_[3].setValue(buf);
    tzTiles_[3].setAccent(a.blue);
    if (d.hasNextTransition) {
      snprintf(buf, sizeof(buf), "Next: clocks %s (%+g h) on %s", d.nextGapHours > 0 ? "forward" : "back",
               d.nextGapHours, d.nextDate);
    } else {
      snprintf(buf, sizeof(buf), "No summer time in this zone.");
    }
    tzNext_.setText(buf);

    snprintf(buf, sizeof(buf), "%+.3f ms", st.offsetMs);
    kpis_[0].setValue(buf);
    snprintf(buf, sizeof(buf), "%+.1f ppb", st.driftPpb);
    kpis_[1].setValue(buf);
    snprintf(buf, sizeof(buf), "%.2f ms", st.rootDispersionMs);
    kpis_[2].setValue(buf);
    snprintf(buf, sizeof(buf), "%.2f ms", st.rootDelayMs);
    kpis_[3].setValue(buf);

    std::string alerts;
    for (const Alert& alert : engine.alerts()) {
      if (!alerts.empty()) alerts += "\n";
      alerts += std::string(alert.title) + ": " + alert.text;
    }
    if (alerts != alertText_.text()) {
      alertText_.setText(alerts.c_str());
      layoutNotes();
    }
    refreshTime();
  }

 private:
  StatusChip<RGB_T> chip_;
  PageLabel<RGB_T> date_, time_, ms_, zoneLine_;
  tinymd::SegmentedButton<RGB_T> scale_;
  int scaleIndex_ = 0;
  CardFrame<RGB_T> tzFrame_;
  PageLabel<RGB_T> tzCaption_, tzName_;
  tinymd::StatTile<RGB_T> tzTiles_[4];
  tinymd::TextBlock<RGB_T> tzNext_;
  PageLabel<RGB_T> syncCaption_;
  CardFrame<RGB_T> syncFrame_;
  KpiRow<RGB_T> kpis_[4];
  tinymd::TextBlock<RGB_T> alertText_, clockNote_;
  int32_t notesY_ = 0;
  uint32_t lastTickMs_ = 0;

  void layoutNotes() {
    Placer p{kMargin, notesY_, this->inner(), this->pageTop()};
    const bool hasAlerts = !alertText_.text().empty();
    p.place(alertText_, hasAlerts ? alertText_.heightFor(this->inner()) : 0, hasAlerts);
    p.place(clockNote_, clockNote_.heightFor(this->inner()));
  }

  /// The time in the selected scale.
  void refreshTime() {
    TimeEngine& engine = this->engine();
    const double nowMs = engine.nowMs();
    const TimeZoneInfo zone = engine.timeZone();
    const int64_t utcSec = static_cast<int64_t>(floor(nowMs / 1000.0));
    const int ms = static_cast<int>(nowMs - utcSec * 1000.0);
    std::tm tm;
    char label[40];
    if (scaleIndex_ == 0) {
      tm = zone.local(utcSec);
      const TimeZoneDetails d = zone.details(utcSec);
      snprintf(label, sizeof(label), "%s (%s)", d.shortName, d.offsetFormatted);
    } else {
      const int64_t shift = scaleIndex_ == 2 ? kLeapSecondsTai : scaleIndex_ == 3 ? kLeapSecondsGps : 0;
      const time_t t = static_cast<time_t>(utcSec + shift);
      gmtime_r(&t, &tm);
      if (scaleIndex_ == 1) snprintf(label, sizeof(label), "UTC");
      if (scaleIndex_ == 2) snprintf(label, sizeof(label), "TAI (+%ds)", kLeapSecondsTai);
      if (scaleIndex_ == 3) snprintf(label, sizeof(label), "GPS (+%ds)", kLeapSecondsGps);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    date_.setText(buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    time_.setText(buf);
    snprintf(buf, sizeof(buf), ".%03d", ms);
    ms_.setText(buf);
    zoneLine_.setText(label);
  }
};

}  // namespace time_services

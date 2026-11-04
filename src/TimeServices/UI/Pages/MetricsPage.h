#pragma once
#include <initializer_list>
#include <math.h>
#include <vector>

#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ServicePage.h"

namespace time_services {

/**
 * @brief Metrics tab: fused offset, drift, offsets per source type and the
 * transport delays over the last 1, 5 or 15 minutes.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class MetricsPage : public ServicePage<RGB_T> {
  using Base = ServicePage<RGB_T>;
  using typename Base::Placer;

 public:
  explicit MetricsPage(PageContext<RGB_T>& ctx) : Base(ctx, "Metrics", drawChartIcon<RGB_T>) {}


  void build() override {
    const Accents<RGB_T>& a = this->accents();
    Placer p = this->pagePlacer();
    window_ = tinymd::SegmentedButton<RGB_T>(tinymd::Bounds());
    for (const char* s : {"1 min", "5 min", "15 min"}) window_.addSegment(s);
    window_.setSelected(0, true);
    window_.onChange = [this](uint32_t mask) {
      for (int i = 0; i < 3; ++i) {
        if (mask & (1u << i)) windowIndex_ = i;
      }
      this->ctx_.refreshSoon();
    };
    this->add(window_);
    p.place(window_, 28, true, 8);

    static const char* const notes[4] = {
        "How the offset of the local clock to the true time changes, estimated from all enabled sources; the "
        "offset at the start of the window is below. Negative means the local clock is ahead.",
        "How fast that estimate is changing. Close to 0 means a steady clock; large values show a correction in "
        "progress.",
        "What each source type measures on its own, as change to the value below. Sources that agree are "
        "combined; one far from the others is "
        "ignored as an outlier.",
        "Round trip to each source. Lower and steadier is better."};
    fused_ = tinymd::LineChart<RGB_T>(tinymd::Bounds(), "Fused offset change (ms)");
    fused_.addSeries(a.cyan);
    drift_ = tinymd::LineChart<RGB_T>(tinymd::Bounds(), "Drift (ppb)");
    drift_.addSeries(a.emerald);
    sources_ = tinymd::LineChart<RGB_T>(tinymd::Bounds(), "Offset per source type, change (ms)");
    sources_.setSeriesLabel(sources_.addSeries(a.cyan), "NTP");
    const int gnss = sources_.addSeries(a.amber);
    sources_.setSeriesLabel(gnss, "GNSS");
    sources_.setSeriesDashed(gnss, true);
    sources_.setSeriesLabel(sources_.addSeries(a.purple), "BLE CTS");
    delay_ = tinymd::LineChart<RGB_T>(tinymd::Bounds(), "Transport delay (ms)");
    delay_.setSeriesLabel(delay_.addSeries(a.cyan), "NTP");
    delay_.setSeriesLabel(delay_.addSeries(a.purple), "BLE CTS");
    tinymd::LineChart<RGB_T>* charts[4] = {&fused_, &drift_, &sources_, &delay_};
    for (int i = 0; i < 4; ++i) {
      this->add(*charts[i]);
      p.place(*charts[i], 150, true, 4);
      notes_[i].setText(notes[i]);
      this->add(notes_[i]);
      p.place(notes_[i], notes_[i].heightFor(this->inner()), true, 12);
    }
  }

  void refresh() override {
    static const size_t windows[3] = {60, 300, 900};
    const size_t window = windows[windowIndex_];
    this->engine().history(history_, window);
    // one point per second, the newest at the right: the X axis is the time
    // before now
    for (tinymd::LineChart<RGB_T>* chart : {&fused_, &drift_, &sources_, &delay_}) {
      chart->setVisiblePoints(static_cast<int>(window));
      chart->setXAxis(-static_cast<float>(window), 0, 4, [](float v, char* b, size_t n) {
        if (v == 0) {
          snprintf(b, n, "now");
        } else if (v <= -120) {
          snprintf(b, n, "%.0fm", v / 60);
        } else {
          snprintf(b, n, "%.0fs", v);
        }
      });
    }
    // the offsets as change since the start of the window: the absolute
    // values (up to 1.8e12 ms before the clock of a board is set) are in the
    // footers
    const double fusedStart = firstValue({&TelemetryPoint::fusedOffsetMs});
    const double sourceStart =
        isfinite(fusedStart) ? fusedStart
                             : firstValue({&TelemetryPoint::ntpOffsetMs, &TelemetryPoint::gpsOffsetMs,
                                           &TelemetryPoint::bleOffsetMs});
    fillRelative(fused_, 0, &TelemetryPoint::fusedOffsetMs, fusedStart);
    fill(drift_, 0, &TelemetryPoint::driftPpb);
    fillRelative(sources_, 0, &TelemetryPoint::ntpOffsetMs, sourceStart);
    fillRelative(sources_, 1, &TelemetryPoint::gpsOffsetMs, sourceStart);
    fillRelative(sources_, 2, &TelemetryPoint::bleOffsetMs, sourceStart);
    fill(delay_, 0, &TelemetryPoint::ntpDelayMs);
    fill(delay_, 1, &TelemetryPoint::bleDelayMs);
    const ClockStatus st = this->engine().status();
    char buf[64];
    if (isfinite(fusedStart)) {
      snprintf(buf, sizeof(buf), "start %+.3f ms, now %+.3f ms", fusedStart, st.offsetMs);
    } else {
      snprintf(buf, sizeof(buf), "now %+.3f ms", st.offsetMs);
    }
    fused_.setFooter(buf);
    if (isfinite(sourceStart)) {
      snprintf(buf, sizeof(buf), "relative to %+.3f ms", sourceStart);
      sources_.setFooter(buf);
    } else {
      sources_.setFooter("");
    }
    snprintf(buf, sizeof(buf), "now %+.1f ppb", st.driftPpb);
    drift_.setFooter(buf);
  }

 private:
  tinymd::SegmentedButton<RGB_T> window_;
  int windowIndex_ = 0;
  tinymd::LineChart<RGB_T> fused_, drift_, sources_, delay_;
  tinymd::TextBlock<RGB_T> notes_[4];
  std::vector<TelemetryPoint> history_;
  std::vector<float> values_;

  /// The first finite value of the window in any of the fields (NaN if none).
  double firstValue(std::initializer_list<double TelemetryPoint::*> fields) const {
    for (const TelemetryPoint& p : history_) {
      for (double TelemetryPoint::*f : fields) {
        if (isfinite(p.*f)) return p.*f;
      }
    }
    return NAN;
  }

  /// The values minus `base` (computed in double, then plotted as float).
  void fillRelative(tinymd::LineChart<RGB_T>& chart, int series, double TelemetryPoint::*field, double base) {
    values_.resize(history_.size());
    for (size_t i = 0; i < history_.size(); ++i) values_[i] = static_cast<float>(history_[i].*field - base);
    chart.setValues(series, values_);
  }

  void fill(tinymd::LineChart<RGB_T>& chart, int series, float TelemetryPoint::*field) {
    values_.resize(history_.size());
    for (size_t i = 0; i < history_.size(); ++i) values_[i] = history_[i].*field;
    chart.setValues(series, values_);
  }
};

}  // namespace time_services

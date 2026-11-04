#pragma once
#include <string>
#include <vector>

#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ServicePage.h"

namespace time_services {

/**
 * @brief Sources tab: one SourceCard per time source with its state,
 * offset, delay and jitter; tap a card for the details and the weight.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class SourcesPage : public ServicePage<RGB_T> {
  using Base = ServicePage<RGB_T>;
  using typename Base::Placer;

 public:
  explicit SourcesPage(PageContext<RGB_T>& ctx) : Base(ctx, "Sources", drawSensorsIcon<RGB_T>) {}


  void build() override {
    title_ = PageLabel<RGB_T>("Time Sources & Disciplining", tinymd::TypographyRole::kBody);
    this->add(title_);
    subtitle_.setText(
        "Reference inputs of the disciplining engine: NTP servers, a GNSS receiver and BLE CTS peers. The weights set "
        "each source's share of the fused offset. Tap a source for its details.");
    this->add(subtitle_);
    for (int i = 0; i < TS_MAX_SOURCES; ++i) {
      SourceCard<RGB_T>& card = cards_[i];
      card.onEnabled = [this, i](bool on) {
        if (i >= this->cfg().sourceCount) return;
        this->cfg().sources[i].enabled = on;
        this->ctx_.configChanged();
      };
      card.onWeight = [this, i](float v) {
        if (i >= this->cfg().sourceCount) return;
        this->cfg().sources[i].weight = v;
        this->ctx_.configChanged();
      };
      card.onToggleExpand = [this, i]() {
        if (i >= this->cfg().sourceCount) return;
        const char* id = this->cfg().sources[i].id;
        expandedId_ = expandedId_ == id ? "" : id;
        this->ctx_.refreshSoon();
      };
      this->add(card);
    }
    refresh();
  }

  void configChanged() override { refresh(); }

  void refresh() override {
    TimeEngine& engine = this->engine();
    const SystemConfig& cfg = this->cfg();
    const GnssState gnss = engine.gnssState();
    char bleState[48];
    engine.bleClientState(bleState, sizeof(bleState));
    Placer p = this->pagePlacer();
    p.place(title_, 14);
    p.place(subtitle_, subtitle_.heightFor(this->inner()), true, 10);
    for (int i = 0; i < TS_MAX_SOURCES; ++i) {
      SourceCard<RGB_T>& card = cards_[i];
      if (i >= cfg.sourceCount) {
        p.place(card, 0, false);
        continue;
      }
      const SourceConfig& s = cfg.sources[i];
      card.setSource(s, engine.telemetry(s.id));
      card.expanded = expandedId_ == s.id;
      card.setExtra(extraLines(s, gnss, bleState));
      p.place(card, card.heightFor(this->inner()), true, 8);
    }
  }

 private:
  PageLabel<RGB_T> title_;
  tinymd::TextBlock<RGB_T> subtitle_;
  SourceCard<RGB_T> cards_[TS_MAX_SOURCES];
  std::string expandedId_;

  /// The state of the receiver / client of a source.
  static std::vector<std::string> extraLines(const SourceConfig& s, const GnssState& gnss, const char* bleState) {
    std::vector<std::string> extra;
    char buf[64];
    if (s.type == SourceType::kGnss) {
      if (!gnss.available) {
        extra.push_back("No GNSS receiver connected");
      } else {
        snprintf(buf, sizeof(buf), "Satellites %d/%d, fix %s", gnss.satsUsed, gnss.satsVisible,
                 gnss.hasFix ? "yes" : "waiting");
        extra.push_back(buf);
        snprintf(buf, sizeof(buf), "PPS: %s",
                 !gnss.ppsConfigured ? "not connected" : gnss.ppsActive ? "active" : "no pulses");
        extra.push_back(buf);
      }
    } else if (s.type == SourceType::kBleCts) {
      snprintf(buf, sizeof(buf), "Client: %s", bleState);
      extra.push_back(buf);
    }
    return extra;
  }
};

}  // namespace time_services

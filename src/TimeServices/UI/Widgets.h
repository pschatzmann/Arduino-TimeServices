#pragma once
#include <math.h>
#include <time.h>

#include <functional>
#include <string>
#include <vector>

#include "TimeServices/Models.h"
#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Theme.h"

namespace time_services {

/**
 * @file Widgets.h
 * @brief The widgets that are specific to the time services. The generic
 * ones come from TinyMaterialDesign: tinymd::Label, tinymd::TextBlock,
 * tinymd::StatTile, tinymd::Card and the setting rows (tinymd::SliderRow,
 * tinymd::SwitchRow, tinymd::RadioRow, tinymd::KeyValueRow).
 */

/// A tinymd::Card used as background of a group of widgets.
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class CardFrame : public tinymd::Card<RGB_T> {
 public:
  CardFrame() {
    this->elevated = false;
    this->largeCorners = true;
  }
  void setAccent(const RGB_T* accent) {
    if (accent != nullptr) {
      this->setOutlineColor(*accent);
    } else {
      this->clearOutlineColor();
    }
  }
};

/// A tinymd::Label on the page background (or on a card with setOnSurface()).
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class PageLabel : public tinymd::Label<RGB_T> {
 public:
  PageLabel() { this->setOnBackground(true); }
  explicit PageLabel(const char* text, tinymd::TypographyRole role = tinymd::TypographyRole::kBody,
                     tinymd::TextAlign align = tinymd::TextAlign::kStart)
      : tinymd::Label<RGB_T>(tinymd::Bounds(), text, role, align) {
    this->setOnBackground(true);
    // small text is secondary text
    if (role == tinymd::TypographyRole::kLabel) this->setVariantColor(true);
  }
  void setOnSurface(bool onSurface) { this->setOnBackground(!onSurface); }
};

/**
 * @brief One metric of the Clock page: name and value on one line, the
 * explanation below (wrapped).
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class KpiRow : public tinymd::Widget<RGB_T> {
 public:
  KpiRow() = default;
  KpiRow(const char* name, const char* explanation, RGB_T color)
      : name_(name), explanation_(explanation), color_(color) {}
  void setValue(const char* value) { value_ = value; }

  int32_t heightFor(int32_t width) const {
    tinygpu::IFont<RGB_T>& small = *this->theme().typography.label;
    return 16 + tinymd::TextBlock<RGB_T>::lineCount(explanation_, width, small) * (textHeight(small) + 3);
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    const tinymd::Bounds& b = this->bounds;
    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    tinygpu::IFont<RGB_T>& body = *t.typography.body;
    drawText(target, small, b.x, b.y + 3, name_.c_str(), t.colors.onSurfaceVariant, t.colors.surface);
    drawText(target, body, b.right() - textWidth(body, value_.c_str()), b.y + 2, value_.c_str(), color_,
             t.colors.surface);
    tinymd::TextBlock<RGB_T> text(tinymd::Bounds(b.x, b.y + 16, b.w, b.h - 16), explanation_.c_str());
    text.setOnSurface(true);
    text.setTheme(t);
    text.draw(target);
  }

 private:
  std::string name_, explanation_, value_ = "--";
  RGB_T color_{};
};

/**
 * @brief Outlined pill with a colored text (e.g. "STRATUM 2 - REF NTP").
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class StatusChip : public tinymd::Widget<RGB_T> {
 public:
  void setText(const char* text) { text_ = text; }
  void setColor(RGB_T color) { color_ = color; }
  /// Center the pill in the bounds.
  bool centered = true;

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    const tinymd::Bounds& b = this->bounds;
    const int32_t w = textWidth(small, text_.c_str()) + 16;
    const int32_t x = centered ? b.centerX() - w / 2 : b.x;
    const RGB_T bg = tinymd::blend(t.colors.background, color_, 0.15f);
    target.fillRoundRect(toPx(x), toPx(b.y), toPx(w), toPx(b.h), toPx(b.h / 2), bg);
    target.drawRoundRect(toPx(x), toPx(b.y), toPx(w), toPx(b.h), toPx(b.h / 2), color_);
    drawText(target, small, x + 8, b.centerY() - textHeight(small) / 2, text_.c_str(), color_, bg);
  }

 private:
  std::string text_;
  RGB_T color_ = Accents<RGB_T>().amber;
};

/// Filled dot (sync state in the app bar).
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class StatusDot : public tinymd::Widget<RGB_T> {
 public:
  void setColor(RGB_T color) { color_ = color; }
  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const tinymd::Bounds& b = this->bounds;
    target.fillCircle(toPx(b.centerX()), toPx(b.centerY()), toPx(b.w / 2), color_);
  }

 private:
  RGB_T color_ = Accents<RGB_T>().amber;
};

/**
 * @brief Card of one time source (Sources page): icon, name, status and an
 * on/off switch; offset, delay and jitter while enabled; tap to expand for
 * the details and the weight slider (tinymd::SliderRow).
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class SourceCard : public tinymd::Widget<RGB_T> {
 public:
  SourceCard() : weight_("Weight", 0, 1, 0.05f, [](float v, char* b, size_t n) { snprintf(b, n, "%.2f", v); }) {
    weight_.setOnSurface(true);
    weight_.onChange = [this](float v) {
      if (onWeight) onWeight(v);
    };
  }

  std::function<void(bool)> onEnabled;
  std::function<void(float)> onWeight;
  std::function<void()> onToggleExpand;

  bool expanded = false;

  void setSource(const SourceConfig& src, const SourceTelemetry& tel) {
    src_ = src;
    tel_ = tel;
    switch_.setValue(src.enabled);
    if (weightSynced_ != src.weight) {
      weight_.setValue(src.weight);
      weightSynced_ = src.weight;
    }
  }
  /// Extra lines of the expanded card (e.g. the GNSS state).
  void setExtra(const std::vector<std::string>& lines) { extra_ = lines; }

  void setTheme(const tinymd::MaterialTheme<RGB_T>& theme) override {
    tinymd::Widget<RGB_T>::setTheme(theme);
    switch_.setTheme(theme);
    weight_.setTheme(theme);
  }

  int32_t heightFor(int32_t width) const {
    int32_t h = kHeader;
    if (src_.enabled) h += kTiles;
    if (expanded) {
      tinygpu::IFont<RGB_T>& small = *this->theme().typography.label;
      h += 6 + 6 * kLine + static_cast<int32_t>(extra_.size()) * kLine;
      h += tinymd::TextBlock<RGB_T>::lineCount(tel_.detail, width - 2 * kPad, small) * (textHeight(small) + 3) + 4;
      h += tinymd::SliderRow<RGB_T>::kHeight;
    }
    return h + kPad;
  }

  void draw(tinygpu::ISurface<RGB_T>& target) override {
    using tinymd::toPx;
    const tinymd::MaterialTheme<RGB_T>& t = this->theme();
    const tinymd::Bounds& b = this->bounds;
    const Accents<RGB_T> a;
    drawCardFrame(target, b, t);
    tinygpu::IFont<RGB_T>& small = *t.typography.label;
    tinygpu::IFont<RGB_T>& body = *t.typography.body;
    const RGB_T sc = statusColor<RGB_T>(tel_.status);
    const RGB_T bg = t.colors.surface;

    // header
    const tinymd::Bounds icon(b.x + kPad, b.y + 8, 22, 22);
    switch (src_.type) {
      case SourceType::kNtp:
        drawGlobeIcon(target, icon, sc);
        break;
      case SourceType::kGnss:
        drawSatelliteIcon(target, icon, sc);
        break;
      case SourceType::kBleCts:
        drawBluetoothIcon(target, icon, sc);
        break;
    }
    const int32_t tx = b.x + kPad + 28;
    std::string name = src_.name;
    const int32_t maxW = b.w - 28 - 2 * kPad - 50;
    while (name.size() > 3 && textWidth(body, name.c_str()) > maxW) name.pop_back();
    drawText(target, body, tx, b.y + 9, name.c_str(), t.colors.onSurface, bg);
    target.fillCircle(toPx(tx + 3), toPx(b.y + 25), 3, sc);
    drawText(target, small, tx + 10, b.y + 22, statusLabel(tel_.status), t.colors.onSurfaceVariant, bg);
    switch_.bounds = tinymd::Bounds(b.right() - kPad - 44, b.y + 8, 44, 24);
    switch_.draw(target);
    int32_t y = b.y + kHeader;

    // offset / delay / jitter
    if (src_.enabled) {
      const int32_t gap = 4;
      const int32_t w = (b.w - 2 * kPad - 2 * gap) / 3;
      char v[3][20];
      snprintf(v[0], sizeof(v[0]), "%+.3f ms", tel_.offsetMs);
      if (hasValue(tel_.delayMs)) {
        snprintf(v[1], sizeof(v[1]), "%.2f ms", tel_.delayMs);
      } else {
        snprintf(v[1], sizeof(v[1]), "--");
      }
      snprintf(v[2], sizeof(v[2]), "%.2f ms", tel_.jitterMs);
      static const char* const captions[3] = {"OFFSET", "DELAY", "JITTER"};
      const RGB_T colors[3] = {a.cyan, a.purple, a.amber};
      for (int i = 0; i < 3; ++i) {
        const int32_t x = b.x + kPad + i * (w + gap);
        target.fillRoundRect(toPx(x), toPx(y), toPx(w), toPx(kTiles - 6), toPx(t.shape.small), t.colors.surfaceVariant);
        drawText(target, small, x + 4, y + 4, captions[i], t.colors.onSurfaceVariant, t.colors.surfaceVariant);
        drawText(target, small, x + 4, y + 17, v[i], colors[i], t.colors.surfaceVariant);
      }
      y += kTiles;
    }

    // details
    if (expanded) {
      target.fillRect(toPx(b.x + kPad), toPx(y), toPx(b.w - 2 * kPad), 1, t.colors.outline);
      y += 6;
      char buf[48];
      auto kv = [&](const char* key, const char* value) {
        drawText(target, small, b.x + kPad, y + 2, key, t.colors.onSurfaceVariant, bg);
        drawText(target, small, b.right() - kPad - textWidth(small, value), y + 2, value, t.colors.onSurface, bg);
        y += kLine;
      };
      snprintf(buf, sizeof(buf), "%03o", tel_.reach);
      kv("Reach", buf);
      snprintf(buf, sizeof(buf), "%u", src_.stratum);
      kv("Stratum", buf);
      snprintf(buf, sizeof(buf), "%lu s", static_cast<unsigned long>(src_.pollIntervalSec));
      kv("Poll interval", buf);
      if (tel_.lastSeenMs > 0) {
        const time_t sec = static_cast<time_t>(tel_.lastSeenMs / 1000.0);
        std::tm tm;
        gmtime_r(&sec, &tm);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d UTC", tm.tm_hour, tm.tm_min, tm.tm_sec);
      } else {
        snprintf(buf, sizeof(buf), "never");
      }
      kv("Last seen", buf);
      kv(src_.type == SourceType::kNtp ? "Host" : src_.type == SourceType::kBleCts ? "Peer" : "Receiver",
         src_.type == SourceType::kGnss ? "serial NMEA" : (src_.host[0] ? src_.host : "first found"));
      for (const std::string& line : extra_) {
        drawText(target, small, b.x + kPad, y + 2, line.c_str(), t.colors.onSurfaceVariant, bg);
        y += kLine;
      }
      y += kLine;  // gap
      tinymd::TextBlock<RGB_T> detail(tel_.detail);
      detail.setOnSurface(true);
      detail.setTheme(t);
      const int32_t dh = detail.heightFor(b.w - 2 * kPad);
      detail.bounds = tinymd::Bounds(b.x + kPad, y, b.w - 2 * kPad, dh);
      detail.draw(target);
      y += dh + 4;
      weight_.bounds = tinymd::Bounds(b.x + kPad, y, b.w - 2 * kPad, tinymd::SliderRow<RGB_T>::kHeight);
      weight_.draw(target);
    }
  }

  bool onGesture(const tinygpu::GestureEvent& event) override {
    const int32_t x = event.startPoint.x, y = event.startPoint.y;
    if (expanded && weight_.bounds.contains(x, y)) return weight_.onGesture(event);
    if (!tinymd::isTapGesture(event.type)) return false;
    const tinymd::Bounds sw(switch_.bounds.x - 8, switch_.bounds.y - 6, switch_.bounds.w + 16, switch_.bounds.h + 12);
    if (sw.contains(x, y)) {
      if (onEnabled) onEnabled(!src_.enabled);
      return true;
    }
    if (onToggleExpand) onToggleExpand();
    return true;
  }

  bool isDraggableAt(int32_t x, int32_t y) const override {
    return expanded && weight_.bounds.contains(x, y) && weight_.isDraggableAt(x, y);
  }

 private:
  static constexpr int32_t kPad = 8;
  static constexpr int32_t kHeader = 38;
  static constexpr int32_t kTiles = 36;
  static constexpr int32_t kLine = 12;
  SourceConfig src_;
  SourceTelemetry tel_;
  std::vector<std::string> extra_;
  tinymd::Switch<RGB_T> switch_;
  tinymd::SliderRow<RGB_T> weight_;
  float weightSynced_ = -1;
};

}  // namespace time_services

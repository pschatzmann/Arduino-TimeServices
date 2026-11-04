#pragma once
#include <string.h>

#include <initializer_list>

#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ServicePage.h"

namespace time_services {

/**
 * @brief A card of the Publish tab: title, state and on/off switch, an
 * error, key/value rows and a note. Used for the NTP server and the BLE CTS.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class ServerCard {
 public:
  static constexpr int kMaxRows = 6;
  std::function<void(bool)> onEnabled;

  /// Adds the widgets to `page`; `keys` are the captions of the rows.
  void build(tinymd::Container<RGB_T>& page, const char* title, std::initializer_list<const char*> keys,
             RGB_T errorColor) {
    title_ = PageLabel<RGB_T>(title, tinymd::TypographyRole::kBody);
    state_ = PageLabel<RGB_T>("", tinymd::TypographyRole::kLabel);
    title_.setOnSurface(true);
    state_.setOnSurface(true);
    error_.setOnSurface(true);
    note_.setOnSurface(true);
    error_.setColor(errorColor);
    switch_.onChange = [this](bool on) {
      if (onEnabled) onEnabled(on);
    };
    page.addChild(frame_);
    page.addChild(title_);
    page.addChild(state_);
    page.addChild(switch_);
    page.addChild(error_);
    rowCount_ = 0;
    for (const char* k : keys) {
      if (rowCount_ >= kMaxRows) break;
      rows_[rowCount_].setCaption(k);
      page.addChild(rows_[rowCount_++]);
    }
  }

  /// Widgets after the rows (added by the page).
  tinymd::KeyValueRow<RGB_T>& row(int i) { return rows_[i]; }
  tinymd::TextBlock<RGB_T>& note() { return note_; }

  void setState(bool enabled, const char* state, RGB_T stateColor, const char* error, const RGB_T* accent) {
    switch_.setValue(enabled);
    state_.setText(state);
    state_.setColor(stateColor);
    error_.setText(error);
    frame_.setAccent(enabled ? accent : nullptr);
  }

  /// Positions the card at y with the given width; `extra` places widgets
  /// between the rows and the note. Returns the bottom of the card.
  template <typename PlacerT, typename Extra>
  int32_t layout(int32_t x, int32_t y, int32_t w, int32_t top, Extra extra) {
    PlacerT c{x + 8, y + 8, w - 16, top};
    switch_.bounds = tinymd::Bounds(x + w - 8 - 44, c.y + 4, 44, 24);
    title_.bounds = tinymd::Bounds(c.x, c.y, c.w - 50, 14);
    state_.bounds = tinymd::Bounds(c.x, c.y + 16, c.w - 50, 12);
    c.y += 34;
    const bool hasError = !error_.text().empty();
    c.place(error_, hasError ? error_.heightFor(c.w) : 0, hasError);
    for (int i = 0; i < rowCount_; ++i) c.place(rows_[i], tinymd::KeyValueRow<RGB_T>::kHeight, true, 2);
    extra(c);
    c.place(note_, note_.heightFor(c.w), true, 0);
    frame_.bounds = tinymd::Bounds(x, y, w, c.y - y + 8);
    return frame_.bounds.bottom();
  }

 private:
  CardFrame<RGB_T> frame_;
  PageLabel<RGB_T> title_, state_;
  tinymd::Switch<RGB_T> switch_;
  tinymd::TextBlock<RGB_T> error_, note_;
  tinymd::KeyValueRow<RGB_T> rows_[kMaxRows];
  int rowCount_ = 0;
};

/**
 * @brief Publish tab: the NTP server and the BLE Current Time Service.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class PublishPage : public ServicePage<RGB_T> {
  using Base = ServicePage<RGB_T>;
  using typename Base::Placer;
  using Base::kMargin;

 public:
  explicit PublishPage(PageContext<RGB_T>& ctx) : Base(ctx, "Publish", drawTowerIcon<RGB_T>) {}


  /// Index of the page that "Server settings" opens.
  int settingsPage = 4;

  void build() override {
    title_ = PageLabel<RGB_T>("Time Publishing Services", tinymd::TypographyRole::kBody);
    this->add(title_);
    subtitle_.setText(
        "Serve the disciplined clock to other devices as an NTP server (UDP) and a Bluetooth LE Current Time Service "
        "peripheral.");
    this->add(subtitle_);

    const RGB_T rose = this->accents().rose;
    ntp_.build(*this, "NTP server (RFC 5905)", {"Time source", "Stratum", "Packets served", "Dropped", "Last client"},
               rose);
    ntp_.onEnabled = [this](bool on) {
      this->cfg().ntpServer.enabled = on;
      this->ctx_.configChanged();
    };
    this->add(ntp_.note());

    ble_.build(*this, "BLE Current Time Service", {"Connected peers", "Notifications", "Time source"}, rose);
    ble_.onEnabled = [this](bool on) {
      this->cfg().bleCts.enabled = on;
      this->ctx_.configChanged();
    };
    previewCaption_ = PageLabel<RGB_T>("0x2A2B Current Time (preview)", tinymd::TypographyRole::kLabel);
    preview_ = PageLabel<RGB_T>("", tinymd::TypographyRole::kLabel);
    preview_.setColor(this->accents().blue);
    previewCaption_.setOnSurface(true);
    preview_.setOnSurface(true);
    this->add(previewCaption_);
    this->add(preview_);
    ble_.note().setText(
        "Characteristics: 0x2A2B Current Time (read/notify), 0x2A0F Local Time Information, 0x2A14 Reference Time "
        "Information.");
    this->add(ble_.note());

    settingsButton_ = tinymd::Button<RGB_T>(tinymd::Bounds(), "Server settings", tinymd::ButtonVariant::kOutlined);
    settingsButton_.onClick = [this]() {
      if (this->ctx_.showPage) this->ctx_.showPage(settingsPage);
    };
    this->add(settingsButton_);
    refresh();
  }

  void configChanged() override { refresh(); }

  void refresh() override {
    TimeEngine& engine = this->engine();
    const SystemConfig& cfg = this->cfg();
    const Accents<RGB_T>& a = this->accents();
    const RGB_T muted = this->theme().colors.onSurfaceVariant;
    const NtpServerState ntp = engine.ntpServerState();
    const BleServerState ble = engine.bleServerState();
    char buf[64];

    const char* ntpState = ntp.waitingForNetwork ? "Waiting for the network"
                           : ntp.error[0]        ? "Error"
                           : ntp.running         ? nullptr
                                                 : "Stopped";
    if (ntpState == nullptr) {
      snprintf(buf, sizeof(buf), "Serving on UDP %u", ntp.port);
      ntpState = buf;
    }
    ntp_.setState(cfg.ntpServer.enabled, ntpState, ntp.error[0] ? a.rose : ntp.running ? a.emerald : muted, ntp.error,
                  &a.cyan);
    ntp_.row(0).setValue(publishedLabel());
    snprintf(buf, sizeof(buf), "%u", cfg.ntpServer.stratum);
    ntp_.row(1).setValue(buf);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(ntp.packetsServed));
    ntp_.row(2).setValue(buf);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(ntp.dropped));
    ntp_.row(3).setValue(buf);
    ntp_.row(4).setValue(ntp.lastClient[0] ? ntp.lastClient : "-");
    char note[96];
    snprintf(note, sizeof(note), "Port %u, ref id %s. Not synchronised: clients get stratum 16.",
             cfg.ntpServer.listenPort, cfg.ntpServer.refId);
    ntp_.note().setText(note);

    const char* bleState = ble.waitingForBluetooth ? "Bluetooth not available"
                           : ble.error[0]          ? "Error"
                           : ble.running           ? "Advertising"
                                                   : "Stopped";
    ble_.setState(cfg.bleCts.enabled, bleState, ble.error[0] ? a.rose : ble.running ? a.emerald : muted, ble.error,
                  &a.blue);
    if (ble.connected >= 0) {
      snprintf(buf, sizeof(buf), "%d", ble.connected);
    } else {
      snprintf(buf, sizeof(buf), "-");
    }
    ble_.row(0).setValue(buf);
    if (cfg.bleCts.sendNotifications) {
      char iv[16];
      snprintf(buf, sizeof(buf), "every %s", formatInterval(iv, sizeof(iv), cfg.bleCts.notifyIntervalMs));
    } else {
      snprintf(buf, sizeof(buf), "off");
    }
    ble_.row(1).setValue(buf);
    ble_.row(2).setValue(publishedLabel());
    preview_.setText(ble.lastPacketHex[0] ? ble.lastPacketHex : "--");

    // layout
    const int32_t w = this->inner();
    Placer p = this->pagePlacer();
    p.place(title_, 14);
    p.place(subtitle_, subtitle_.heightFor(w), true, 10);
    p.y = ntp_.template layout<Placer>(kMargin, p.y, w, this->pageTop(), [](Placer&) {}) + 10;
    p.y = ble_.template layout<Placer>(kMargin, p.y, w, this->pageTop(), [this](Placer& c) {
            c.place(previewCaption_, 12, true, 2);
            c.place(preview_, 12, true, 4);
          }) +
          10;
    p.place(settingsButton_, 32);
  }

 private:
  PageLabel<RGB_T> title_;
  tinymd::TextBlock<RGB_T> subtitle_;
  ServerCard<RGB_T> ntp_, ble_;
  PageLabel<RGB_T> previewCaption_, preview_;
  tinymd::Button<RGB_T> settingsButton_;

  const char* publishedLabel() {
    const SystemConfig& cfg = this->cfg();
    if (strcmp(cfg.publishSourceId, kPublishFused) == 0) return "Fused";
    if (strcmp(cfg.publishSourceId, kPublishSystem) == 0) return "System clock";
    const SourceConfig* s = cfg.find(cfg.publishSourceId);
    return s != nullptr ? s->name : "(missing)";
  }
};

}  // namespace time_services

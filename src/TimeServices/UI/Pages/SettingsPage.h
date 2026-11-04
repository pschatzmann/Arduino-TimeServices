#pragma once
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <functional>

#include "TimeServices/TimeZoneInfo.h"
#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ServicePage.h"

namespace time_services {

/**
 * @brief One NTP server in the Settings: host field (validated: it shows an
 * error text), on/off switch and delete button.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
struct HostRow {
  tinymd::TextField<RGB_T> field;
  tinymd::Switch<RGB_T> enabled;
  tinymd::IconButton<RGB_T> remove;
  /// Index in SystemConfig::sources, -1 = unused.
  int source = -1;

  bool used() const { return source >= 0; }
  bool valid() const { return field.isValid(); }

  void addTo(tinymd::Container<RGB_T>& page) {
    page.addChild(field);
    page.addChild(enabled);
    page.addChild(remove);
  }
};

/**
 * @brief Settings tab: NTP servers, published source, NTP server, BLE CTS,
 * disciplining, time zone, system clock, theme and reset to defaults.
 *
 * The sliders, switches and chips are applied at once; the text fields when
 * Done is tapped on the keyboard or the tab is left.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class SettingsPage : public ServicePage<RGB_T> {
  using Base = ServicePage<RGB_T>;
  using typename Base::Placer;
  using Base::kMargin;

 public:
  explicit SettingsPage(PageContext<RGB_T>& ctx)
      : Base(ctx, "Settings", drawTuneIcon<RGB_T>),
        stratum_("Stratum", 1, 15, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f", v); }),
        rate_("Rate limit (requests/min per client)", 0, 600, 10,
              [](float v, char* b, size_t n) { v > 0 ? snprintf(b, n, "%.0f", v) : snprintf(b, n, "off"); }),
        tau_("PLL time constant", 1, 120, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f s", v); }),
        fll_("FLL bandwidth", 0, 0.5f, 0.01f, [](float v, char* b, size_t n) { snprintf(b, n, "%.2f Hz", v); }),
        slew_("Max. slew", 10, 1000, 10, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f ppm", v); }),
        holdover_("Holdover timeout", 1, 48, 1, [](float v, char* b, size_t n) { snprintf(b, n, "%.0f h", v); }),
        adv_("Advertising interval", 100, 5000, 100,
             [](float v, char* b, size_t n) { snprintf(b, n, "%.0f ms", v); }),
        notify_("Send notifications"),
        prefer1_("Prefer stratum 1"),
        stepClock_("Step system clock"),
        dark_("Dark theme") {}


  void build() override {
    tinymd::Keyboard<RGB_T>& keyboard = this->ctx_.keyboard;

    // NTP servers
    ntpCaption_ = PageLabel<RGB_T>("NTP time servers", tinymd::TypographyRole::kBody);
    this->add(ntpCaption_);
    ntpNote_.setText("Servers that are queried over UDP: host or host:port. Changes apply when you leave a field.");
    this->add(ntpNote_);
    for (int i = 0; i < TS_MAX_SOURCES; ++i) {
      HostRow<RGB_T>& row = hosts_[i];
      row.field = tinymd::TextField<RGB_T>(tinymd::Bounds(), "Host", "time.example.com");
      row.field.setMaxLength(sizeof(SourceConfig::host) - 1);
      row.field.setOnBackground(true);
      row.field.setValidator([](const std::string& t) { return isValidNtpHost(t.c_str()); },
                             "Use host or host:port");
      row.field.onChange = [this, i](const std::string&) {
        if (hosts_[i].used() && hosts_[i].field.hasError() != hasErrorRoom_[i]) layout();
      };
      row.field.onBlur = [this]() { applyTextFields(); };
      keyboard.manage(row.field);
      row.enabled.onChange = [this, i](bool on) {
        if (!hosts_[i].used()) return;
        this->cfg().sources[hosts_[i].source].enabled = on;
        this->ctx_.configChanged();
      };
      row.remove = tinymd::IconButton<RGB_T>(tinymd::Bounds(), tinymd::drawClose<RGB_T>);
      row.remove.onClick = [this, i]() { removeHost(i); };
      row.addTo(*this);
    }
    quickCaption_ = PageLabel<RGB_T>("Quick add", tinymd::TypographyRole::kLabel);
    this->add(quickCaption_);
    for (int i = 0; i < kPresetCount; ++i) {
      presets_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), kPresets[i]);
      presets_[i].onClick = [this, i]() { addHost(kPresets[i]); };
      this->add(presets_[i]);
    }
    addServer_ = tinymd::Button<RGB_T>(tinymd::Bounds(), "Add server", tinymd::ButtonVariant::kOutlined);
    addServer_.onClick = [this]() { addHost(""); };
    this->add(addServer_);

    // published source
    pubCaption_ = PageLabel<RGB_T>("Published time source", tinymd::TypographyRole::kBody);
    this->add(pubCaption_);
    pubNote_.setText(
        "Which clock the NTP and BLE CTS servers hand out. If the chosen source has no recent sample, the servers "
        "report it as unsynchronised.");
    this->add(pubNote_);
    for (int i = 0; i < TS_MAX_SOURCES + 2; ++i) {
      published_[i].onSelected = [this, i]() {
        SystemConfig& c = this->cfg();
        if (i == 0) {
          copyText(c.publishSourceId, kPublishFused);
        } else if (i == 1) {
          copyText(c.publishSourceId, kPublishSystem);
        } else if (i - 2 < c.sourceCount) {
          copyText(c.publishSourceId, c.sources[i - 2].id);
        }
        this->ctx_.configChanged();
      };
      this->add(published_[i]);
    }

    // NTP server
    srvCaption_ = PageLabel<RGB_T>("NTP server", tinymd::TypographyRole::kBody);
    this->add(srvCaption_);
    srvNote_.setText("Settings of the NTP server that this device offers. Switch it on in the Publish tab.");
    this->add(srvNote_);
    port_ = tinymd::TextField<RGB_T>(tinymd::Bounds(), "UDP port", "123");
    port_.setMaxLength(5);
    port_.setInputType(tinymd::InputType::kNumber);
    port_.setOnBackground(true);
    port_.setValidator(
        [](const std::string& t) {
          const long v = atol(t.c_str());
          return v >= 1 && v <= 65535;
        },
        "1 - 65535");
    port_.onBlur = [this]() { applyTextFields(); };
    keyboard.manage(port_);
    this->add(port_);
    refId_ = tinymd::TextField<RGB_T>(tinymd::Bounds(), "Reference id", "CHRN");
    refId_.setMaxLength(4);
    refId_.setOnBackground(true);
    refId_.onBlur = [this]() { applyTextFields(); };
    keyboard.manage(refId_);
    this->add(refId_);
    leapCaption_ = PageLabel<RGB_T>("Leap indicator", tinymd::TypographyRole::kLabel);
    this->add(leapCaption_);
    static const char* const leaps[4] = {"None", "+1 s", "-1 s", "Unsync"};
    for (int i = 0; i < 4; ++i) {
      leapChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), leaps[i], true);
      leapChips_[i].onClick = [this, i]() {
        this->cfg().ntpServer.leapIndicator = static_cast<uint8_t>(i);
        this->ctx_.configChanged();
      };
      this->add(leapChips_[i]);
    }
    addSlider(stratum_, [this](float v) { this->cfg().ntpServer.stratum = static_cast<uint8_t>(v); });
    addSlider(rate_, [this](float v) { this->cfg().ntpServer.rateLimitPerMin = static_cast<uint16_t>(v); });
    subnets_ = tinymd::TextField<RGB_T>(tinymd::Bounds(), "Allowed subnets", "0.0.0.0/0");
    subnets_.setMaxLength(sizeof(NtpServerConfig::allowSubnets) - 1);
    subnets_.setOnBackground(true);
    subnets_.onBlur = [this]() { applyTextFields(); };
    keyboard.manage(subnets_);
    this->add(subnets_);

    // BLE CTS
    bleCaption_ = PageLabel<RGB_T>("BLE Current Time Service", tinymd::TypographyRole::kBody);
    this->add(bleCaption_);
    bleNote_.setText("Settings of the Bluetooth Current Time Service. Switch it on in the Publish tab.");
    this->add(bleNote_);
    addSlider(adv_, [this](float v) { this->cfg().bleCts.advIntervalMs = static_cast<uint16_t>(v); });
    addSwitch(notify_, [this](bool v) { this->cfg().bleCts.sendNotifications = v; });
    for (int i = 0; i < kNotifyCount; ++i) {
      notifyChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), kNotifyLabels[i], true);
      notifyChips_[i].onClick = [this, i]() {
        this->cfg().bleCts.notifyIntervalMs = kNotifyMs[i];
        this->ctx_.configChanged();
      };
      this->add(notifyChips_[i]);
    }

    // disciplining
    discCaption_ = PageLabel<RGB_T>("Disciplining", tinymd::TypographyRole::kBody);
    this->add(discCaption_);
    discNote_.setText(
        "How the readings of the time sources are combined into one clock estimate, and how quickly that estimate "
        "follows them.");
    this->add(discNote_);
    static const char* const algos[3] = {"Kalman", "Marzullo", "PLL/FLL"};
    for (int i = 0; i < 3; ++i) {
      algoChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), algos[i], true);
      algoChips_[i].onClick = [this, i]() {
        this->cfg().disciplining.algorithm = static_cast<Algorithm>(i);
        this->ctx_.configChanged();
      };
      this->add(algoChips_[i]);
    }
    addSlider(tau_, [this](float v) { this->cfg().disciplining.pllTimeConstantSec = static_cast<int>(v); });
    addSlider(fll_, [this](float v) { this->cfg().disciplining.fllBandwidthHz = v; });
    addSlider(slew_, [this](float v) { this->cfg().disciplining.maxSlewPpm = static_cast<int>(v); });
    addSlider(holdover_, [this](float v) { this->cfg().disciplining.holdoverTimeoutSec = static_cast<int>(v) * 3600; });
    addSwitch(prefer1_, [this](bool v) { this->cfg().disciplining.preferStratum1 = v; });

    // time zone
    zoneCaption_ = PageLabel<RGB_T>("Time zone", tinymd::TypographyRole::kBody);
    this->add(zoneCaption_);
    for (int i = 0; i < kZoneCount; ++i) {
      zoneChips_[i] = tinymd::Chip<RGB_T>(tinymd::Bounds(), kZones[i].label, true);
      zoneChips_[i].onClick = [this, i]() {
        copyText(this->cfg().timeZone, kZones[i].iana);
        this->ctx_.configChanged();
      };
      this->add(zoneChips_[i]);
    }

    // device
    deviceCaption_ = PageLabel<RGB_T>("Device", tinymd::TypographyRole::kBody);
    this->add(deviceCaption_);
    addSwitch(stepClock_, [this](bool v) { this->cfg().stepSystemClock = v; });
    stepNote_.setText(
        "Sets the clock of the device (settimeofday()) to the fused time when they differ by more than 128 ms, so "
        "that time() is correct as well.");
    this->add(stepNote_);
    dark_.onChange = [this](bool v) {
      if (this->ctx_.setDark) this->ctx_.setDark(v);
    };
    this->add(dark_);

    defaultsButton_ = tinymd::Button<RGB_T>(tinymd::Bounds(), "Reset to defaults", tinymd::ButtonVariant::kOutlined);
    defaultsButton_.onClick = [this]() { this->ctx_.app.screen().presentDialog(defaultsDialog_); };
    this->add(defaultsButton_);
    buildDefaultsDialog();
    // room to scroll the last text field above the keyboard
    this->add(keyboardSpacer_);
    configChanged();
  }

  /// Updates the widgets from the configuration and positions them.
  void configChanged() override {
    const SystemConfig& cfg = this->cfg();
    int row = 0;
    for (HostRow<RGB_T>& h : hosts_) h.source = -1;
    for (int i = 0; i < cfg.sourceCount && row < TS_MAX_SOURCES; ++i) {
      if (cfg.sources[i].type != SourceType::kNtp) continue;
      HostRow<RGB_T>& h = hosts_[row++];
      h.source = i;
      // do not overwrite what is being typed
      if (!h.field.focused() && h.field.text() != cfg.sources[i].host) h.field.setText(cfg.sources[i].host);
      h.enabled.setValue(cfg.sources[i].enabled);
    }

    published_[0].setCaption("Fused (all sources)");
    published_[1].setCaption("System clock");
    published_[0].setSelected(strcmp(cfg.publishSourceId, kPublishFused) == 0);
    published_[1].setSelected(strcmp(cfg.publishSourceId, kPublishSystem) == 0);
    for (int i = 0; i < cfg.sourceCount; ++i) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s%s", cfg.sources[i].name, cfg.sources[i].enabled ? "" : " (off)");
      published_[i + 2].setCaption(buf);
      published_[i + 2].setSelected(strcmp(cfg.publishSourceId, cfg.sources[i].id) == 0);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", cfg.ntpServer.listenPort);
    if (!port_.focused() && port_.text() != buf) port_.setText(buf);
    if (!subnets_.focused() && subnets_.text() != cfg.ntpServer.allowSubnets) subnets_.setText(cfg.ntpServer.allowSubnets);
    if (!refId_.focused() && refId_.text() != cfg.ntpServer.refId) refId_.setText(cfg.ntpServer.refId);
    for (int i = 0; i < 4; ++i) leapChips_[i].setSelected(cfg.ntpServer.leapIndicator == i);
    adv_.setValue(cfg.bleCts.advIntervalMs);
    stratum_.setValue(cfg.ntpServer.stratum);
    rate_.setValue(cfg.ntpServer.rateLimitPerMin);
    notify_.setValue(cfg.bleCts.sendNotifications);
    for (int i = 0; i < kNotifyCount; ++i) notifyChips_[i].setSelected(cfg.bleCts.notifyIntervalMs == kNotifyMs[i]);
    for (int i = 0; i < 3; ++i) algoChips_[i].setSelected(static_cast<int>(cfg.disciplining.algorithm) == i);
    tau_.setValue(cfg.disciplining.pllTimeConstantSec);
    fll_.setValue(cfg.disciplining.fllBandwidthHz);
    slew_.setValue(cfg.disciplining.maxSlewPpm);
    holdover_.setValue(cfg.disciplining.holdoverTimeoutSec / 3600.0f);
    prefer1_.setValue(cfg.disciplining.preferStratum1);
    for (int i = 0; i < kZoneCount; ++i) zoneChips_[i].setSelected(strcmp(cfg.timeZone, kZones[i].iana) == 0);
    stepClock_.setValue(cfg.stepSystemClock);
    if (this->ctx_.isDark) dark_.setValue(this->ctx_.isDark());
    layout();
  }

  /// Leaving the tab applies the text fields.
  void onHide() override { applyTextFields(); }

 private:
  static constexpr int kPresetCount = 7;
  static constexpr int kNotifyCount = 5;
  static constexpr int32_t kKeyboardHeight = 170;
  static constexpr const char* kPresets[kPresetCount] = {
      "pool.ntp.org", "time.nist.gov", "time.windows.com", "time.apple.com", "ptbtime1.ptb.de", "time.cloudflare.com",
      "time.google.com"};
  static constexpr uint32_t kNotifyMs[kNotifyCount] = {1000, 10000, 60000, 600000, 3600000};
  static constexpr const char* kNotifyLabels[kNotifyCount] = {"1 s", "10 s", "1 min", "10 min", "1 h"};

  PageLabel<RGB_T> ntpCaption_;
  tinymd::TextBlock<RGB_T> ntpNote_;
  HostRow<RGB_T> hosts_[TS_MAX_SOURCES];
  PageLabel<RGB_T> quickCaption_;
  tinymd::Chip<RGB_T> presets_[kPresetCount];
  tinymd::Button<RGB_T> addServer_;
  PageLabel<RGB_T> pubCaption_;
  tinymd::TextBlock<RGB_T> pubNote_;
  tinymd::RadioRow<RGB_T> published_[TS_MAX_SOURCES + 2];
  PageLabel<RGB_T> srvCaption_;
  tinymd::TextBlock<RGB_T> srvNote_;
  tinymd::TextField<RGB_T> port_, subnets_, refId_;
  PageLabel<RGB_T> leapCaption_;
  tinymd::Chip<RGB_T> leapChips_[4];
  bool hasErrorRoom_[TS_MAX_SOURCES] = {};
  tinymd::SliderRow<RGB_T> stratum_, rate_;
  PageLabel<RGB_T> bleCaption_;
  tinymd::TextBlock<RGB_T> bleNote_;
  tinymd::SliderRow<RGB_T> tau_, fll_, slew_, holdover_, adv_;
  bool applying_ = false;
  tinymd::SwitchRow<RGB_T> notify_, prefer1_;
  tinymd::Chip<RGB_T> notifyChips_[kNotifyCount];
  PageLabel<RGB_T> discCaption_;
  tinymd::TextBlock<RGB_T> discNote_;
  tinymd::Chip<RGB_T> algoChips_[3];
  PageLabel<RGB_T> zoneCaption_;
  tinymd::Chip<RGB_T> zoneChips_[kZoneCount];
  PageLabel<RGB_T> deviceCaption_;
  tinymd::SwitchRow<RGB_T> stepClock_, dark_;
  tinymd::TextBlock<RGB_T> stepNote_;
  tinymd::Button<RGB_T> defaultsButton_;
  tinymd::Dialog<RGB_T> defaultsDialog_;
  tinymd::Button<RGB_T> defaultsCancel_, defaultsConfirm_;
  PageLabel<RGB_T> keyboardSpacer_;

  void addSlider(tinymd::SliderRow<RGB_T>& slider, std::function<void(float)> apply) {
    slider.onChange = [this, apply](float v) {
      apply(v);
      this->ctx_.configChanged();
    };
    this->add(slider);
  }

  void addSwitch(tinymd::SwitchRow<RGB_T>& row, std::function<void(bool)> apply) {
    row.onChange = [this, apply](bool v) {
      apply(v);
      this->ctx_.configChanged();
    };
    this->add(row);
  }

  void buildDefaultsDialog() {
    const int32_t dialogH = 150;
    defaultsDialog_ =
        tinymd::Dialog<RGB_T>(tinymd::Bounds(16, (this->screenHeight() - dialogH) / 2, this->width() - 32, dialogH),
                              "Reset settings?", "All settings are set back to their default values.");
    defaultsCancel_ = tinymd::Button<RGB_T>(defaultsDialog_.actionRect(0, 2), "Cancel", tinymd::ButtonVariant::kText);
    defaultsCancel_.onClick = [this]() { this->ctx_.app.screen().dismissDialog(); };
    defaultsConfirm_ = tinymd::Button<RGB_T>(defaultsDialog_.actionRect(1, 2), "Reset");
    defaultsConfirm_.onClick = [this]() {
      this->ctx_.app.screen().dismissDialog();
      for (HostRow<RGB_T>& h : hosts_) h.field.blur();
      this->cfg() = defaultConfig();
      if (this->ctx_.setDark) this->ctx_.setDark(true);
      this->ctx_.configChanged();
    };
    defaultsDialog_.addAction(defaultsCancel_);
    defaultsDialog_.addAction(defaultsConfirm_);
  }

  void layout() {
    const int32_t w = this->inner();
    const int32_t top = this->pageTop();
    const SystemConfig& cfg = this->cfg();
    Placer p = this->pagePlacer();
    auto note = [&](tinymd::TextBlock<RGB_T>& t) { p.place(t, t.heightFor(w), true, 8); };
    // chips in a grid; show(i) decides which are visible
    auto chips = [&](tinymd::Chip<RGB_T>* chip, int count, int columns, const std::function<bool(int)>& show) {
      const int32_t gap = 4;
      const int32_t cw = (w - (columns - 1) * gap) / columns;
      int shown = 0;
      for (int i = 0; i < count; ++i) {
        chip[i].visible = show(i);
        if (!chip[i].visible) {
          chip[i].bounds = tinymd::Bounds(kMargin, top, 0, 0);
          continue;
        }
        chip[i].bounds = tinymd::Bounds(kMargin + (shown % columns) * (cw + gap), p.y + (shown / columns) * 30, cw, 26);
        shown++;
      }
      p.y += ((shown + columns - 1) / columns) * 30 + 6;
    };
    auto all = [](int) { return true; };

    p.place(ntpCaption_, 14, true, 2);
    note(ntpNote_);
    for (int i = 0; i < TS_MAX_SOURCES; ++i) {
      HostRow<RGB_T>& h = hosts_[i];
      const bool used = h.used();
      h.field.visible = h.enabled.visible = h.remove.visible = used;
      if (!used) {
        h.field.bounds = h.enabled.bounds = h.remove.bounds = tinymd::Bounds(kMargin, top, 0, 0);
        continue;
      }
      // the field needs 14 px more for its error text
      hasErrorRoom_[i] = h.field.hasError();
      const int32_t fieldH = hasErrorRoom_[i] ? 58 : 44;
      h.field.bounds = tinymd::Bounds(kMargin, p.y, w - 44 - 36 - 8, fieldH);
      h.enabled.bounds = tinymd::Bounds(kMargin + w - 44 - 36 - 2, p.y + 14, 44, 24);
      h.remove.bounds = tinymd::Bounds(kMargin + w - 34, p.y + 8, 34, 34);
      p.y += fieldH + 4;
    }
    p.place(quickCaption_, 12, true, 4);
    chips(presets_, kPresetCount, 2, [this](int i) { return !hasHost(kPresets[i]); });
    addServer_.enabled = cfg.sourceCount < TS_MAX_SOURCES;
    p.place(addServer_, 32, true, 16);

    p.place(pubCaption_, 14, true, 2);
    note(pubNote_);
    for (int i = 0; i < TS_MAX_SOURCES + 2; ++i) {
      p.place(published_[i], tinymd::RadioRow<RGB_T>::kHeight, i < cfg.sourceCount + 2, 2);
    }
    p.y += 14;

    p.place(srvCaption_, 14, true, 2);
    note(srvNote_);
    p.place(port_, port_.hasError() ? 58 : 44);
    p.place(refId_, 44);
    p.place(leapCaption_, 12, true, 4);
    chips(leapChips_, 4, 4, all);
    p.place(stratum_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(rate_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(subnets_, 44, true, 16);

    p.place(bleCaption_, 14, true, 2);
    note(bleNote_);
    p.place(adv_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(notify_, tinymd::SwitchRow<RGB_T>::kHeight);
    const bool notifications = cfg.bleCts.sendNotifications;
    chips(notifyChips_, kNotifyCount, 3, [notifications](int) { return notifications; });
    p.y += 10;

    p.place(discCaption_, 14, true, 2);
    note(discNote_);
    chips(algoChips_, 3, 3, all);
    p.place(tau_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(fll_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(slew_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(holdover_, tinymd::SliderRow<RGB_T>::kHeight);
    p.place(prefer1_, tinymd::SwitchRow<RGB_T>::kHeight, true, 16);

    p.place(zoneCaption_, 14, true, 4);
    chips(zoneChips_, kZoneCount, 2, all);
    p.y += 10;

    p.place(deviceCaption_, 14, true, 2);
    p.place(stepClock_, tinymd::SwitchRow<RGB_T>::kHeight, true, 2);
    note(stepNote_);
    p.place(dark_, tinymd::SwitchRow<RGB_T>::kHeight, true, 12);
    p.place(defaultsButton_, 32);
    p.place(keyboardSpacer_, kKeyboardHeight - 56);
    this->ctx_.app.screen().invalidate();
  }

  bool hasHost(const char* host) {
    const SystemConfig& cfg = this->cfg();
    for (int i = 0; i < cfg.sourceCount; ++i) {
      if (cfg.sources[i].type == SourceType::kNtp && strcasecmp(cfg.sources[i].host, host) == 0) return true;
    }
    return false;
  }

  void addHost(const char* host) {
    SystemConfig& cfg = this->cfg();
    if (cfg.sourceCount >= TS_MAX_SOURCES || (host[0] && hasHost(host))) return;
    char id[16];
    snprintf(id, sizeof(id), "ntp_%lu", static_cast<unsigned long>(millis() % 100000000UL));
    while (cfg.find(id) != nullptr) snprintf(id, sizeof(id), "ntp_%lu", static_cast<unsigned long>(random(100000000L)));
    cfg.addSource(SourceConfig::make(id, host[0] ? host : "NTP server", SourceType::kNtp, true, 2, 0.25f, 16, host));
    this->ctx_.configChanged();
  }

  void removeHost(int row) {
    HostRow<RGB_T>& h = hosts_[row];
    if (!h.used()) return;
    SystemConfig& cfg = this->cfg();
    if (strcmp(cfg.publishSourceId, cfg.sources[h.source].id) == 0) copyText(cfg.publishSourceId, kPublishFused);
    cfg.removeSource(h.source);
    h.field.blur();
    this->ctx_.keyboard.hide();
    this->ctx_.configChanged();
  }

  /// Takes over the text fields (valid entries only).
  void applyTextFields() {
    if (applying_) return;  // blur() below calls onBlur again
    applying_ = true;
    SystemConfig& cfg = this->cfg();
    bool changed = false;
    for (HostRow<RGB_T>& h : hosts_) {
      h.field.blur();
      if (!h.used() || !h.valid()) continue;
      SourceConfig& s = cfg.sources[h.source];
      const char* text = h.field.text().c_str();
      if (strcmp(text, s.host) == 0) continue;
      // a source named after its host follows the host
      if (strcmp(s.name, s.host) == 0 || strcmp(s.name, "NTP server") == 0) copyText(s.name, text);
      copyText(s.host, text);
      changed = true;
    }
    port_.blur();
    subnets_.blur();
    const long port = atol(port_.text().c_str());
    if (port >= 1 && port <= 65535 && port != cfg.ntpServer.listenPort) {
      cfg.ntpServer.listenPort = static_cast<uint16_t>(port);
      changed = true;
    }
    if (!refId_.text().empty() && refId_.text() != cfg.ntpServer.refId) {
      copyText(cfg.ntpServer.refId, refId_.text().c_str());
      changed = true;
    }
    refId_.blur();
    const char* subnets = subnets_.text().empty() ? "0.0.0.0/0" : subnets_.text().c_str();
    if (strcmp(subnets, cfg.ntpServer.allowSubnets) != 0) {
      copyText(cfg.ntpServer.allowSubnets, subnets);
      changed = true;
    }
    if (changed) {
      this->ctx_.configChanged();
    } else {
      configChanged();  // shows the stored values again
    }
    applying_ = false;
  }
};

}  // namespace time_services

#pragma once
#include <functional>
#include <vector>

#include "TimeServices/TimeEngine.h"
#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Pages/ClockPage.h"
#include "TimeServices/UI/Pages/MetricsPage.h"
#include "TimeServices/UI/Pages/PublishPage.h"
#include "TimeServices/UI/Pages/ServicePage.h"
#include "TimeServices/UI/Pages/SettingsPage.h"
#include "TimeServices/UI/Pages/SourcesPage.h"
#include "TimeServices/UI/Theme.h"
#include "TimeServices/UI/Widgets.h"
#include "TinyMaterialDesign.h"

namespace time_services {

/**
 * @brief The touch GUI of the time services on top of a TimeEngine, built
 * with TinyMaterialDesign (port of the Android app's screens).
 *
 * The app provides the frame: the app bar (sync state, menu with Sync now,
 * Light/Dark theme and Shut down), the navigation bar and the on-screen
 * keyboard. Each tab is its own ServicePage (a tinymd::Page, switched by a
 * tinymd::PageNavigator):
 *
 * - ClockPage: the disciplined time, the time zone, the synchronisation
 * - SourcesPage: the time sources
 * - PublishPage: the NTP server and the BLE Current Time Service
 * - MetricsPage: charts of offset, drift and delays
 * - SettingsPage: all settings
 *
 * Further pages can be added with addPage() before begin().
 *
 * With setWifi() the app has a WiFi page (tinymd::WifiLoginPage: networks,
 * SSID, password) in the menu; it is shown on startup when the device has no
 * stored credentials, or cannot connect within 15 s.
 *
 *   TimeEngine engine;
 *   TimeServicesApp<RGB565> gui(board, engine);
 *
 *   void setup() {
 *     engine.begin(defaultConfig());
 *     gui.begin();
 *   }
 *   void loop() {
 *     engine.tick();
 *     gui.update();
 *   }
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class TimeServicesApp {
 public:
  enum Page { kClock = 0, kSources = 1, kPublish = 2, kMetrics = 3, kSettings = 4 };

  /// Called (2 s after the last change) when the user changed a setting,
  /// e.g. to persist it.
  std::function<void(const SystemConfig& config, bool dark)> onSettingsChanged;

  /// Called when the user confirmed "Shut down" in the menu. Without a
  /// callback the menu has no "Shut down".
  std::function<void()> onShutdown;

  TimeServicesApp(tinygpu::LCDBoard& board, TimeEngine& engine)
      : darkTheme_(timeServicesDarkTheme<RGB_T>()),
        lightTheme_(timeServicesLightTheme<RGB_T>()),
        app_(board, darkTheme_),
        ctx_(engine, app_, keyboard_),
        clock_(ctx_),
        sources_(ctx_),
        publish_(ctx_),
        metrics_(ctx_),
        settings_(ctx_),
        navigator_(app_.screen(), nav_),
        wifiPage_(keyboard_, app_.screen()) {
    pages_ = {&clock_, &sources_, &publish_, &metrics_, &settings_};
    ctx_.onConfigChanged = [this]() { configChanged(); };
    ctx_.showPage = [this](int page) { showPage(page); };
    ctx_.setDark = [this](bool dark) {
      setDark(dark);
      configChanged();
    };
    ctx_.isDark = [this]() { return dark_; };
    ctx_.requestRefresh = [this]() { forceRefresh_ = true; };
  }

  /// Adds a page (before begin()).
  void addPage(ServicePage<RGB_T>& page) { pages_.push_back(&page); }

  /// The WiFi of the device (before begin()): adds the WiFi page.
  void setWifi(tinymd::WifiControl& wifi) { wifi_ = &wifi; }

  /// Time on startup until the WiFi page is shown when the stored
  /// credentials do not connect.
  void setWifiStartupTimeoutMs(uint32_t ms) { wifiTimeoutMs_ = ms; }

  /// Builds the GUI and starts the board (display + touch).
  void begin(bool darkTheme = true) {
    dark_ = darkTheme;
    app_.begin();
    if (!darkTheme) app_.screen().setTheme(lightTheme_);
    ctx_.cfg = ctx_.engine.config();
    ctx_.top = kBarHeight;
    ctx_.height = height() - kBarHeight - kNavHeight;
    keyboard_.bounds = tinymd::Bounds(0, height() - kKeyboardHeight, width(), kKeyboardHeight);
    keyboard_.visible = false;
    buildFrame();
    nav_ = tinymd::NavigationBar<RGB_T>(tinymd::Bounds(0, height() - kNavHeight, width(), kNavHeight));
    for (ServicePage<RGB_T>* page : pages_) navigator_.addPage(*page);
    navigator_.onPageChanged = [this](int) {
      keyboard_.hide();
      hideWifi();
      forceRefresh_ = true;
    };
    navigator_.begin(tinymd::Bounds(0, ctx_.top, width(), ctx_.height));  // builds the pages
    // a tab also leaves the WiFi page when it was the current page before
    nav_.onChange = [this](int index) { showPage(index); };
    if (wifi_ != nullptr) {
      // not a tab: shown instead of the pages by showWifi()
      wifiPage_.setWifi(wifi_);
      wifiPage_.onDone = [this]() { showPage(navigator_.current() >= 0 ? navigator_.current() : kClock); };
      wifiPage_.bounds = tinymd::Bounds(0, ctx_.top, width(), ctx_.height);
      wifiPage_.visible = false;
      app_.screen().addWidget(wifiPage_);
      wifiPage_.build();
    }
    app_.screen().addFixedWidget(keyboard_);  // last: on top of everything
    beginMs_ = millis();
    built_ = true;
  }

  /// Call from loop(): handles touch input and redraws when needed.
  void update() {
    const uint32_t now = millis();
    checkWifiOnStartup(now);
    bool redraw = wifiVisible_ ? wifiPage_.tick(now) : navigator_.tick(now);
    if (now - lastRefreshMs_ >= refreshIntervalMs_) {
      const uint32_t rev = ctx_.engine.revision();
      if (rev != lastRevision_ || forceRefresh_) {
        lastRevision_ = rev;
        lastRefreshMs_ = now;
        forceRefresh_ = false;
        syncDot_.setColor(ctx_.engine.status().synced ? ctx_.accents.emerald : ctx_.accents.amber);
        if (!wifiVisible_) pages_[navigator_.current()]->refresh();
        redraw = true;
      }
    }
    if (redraw) app_.screen().invalidate();
    if (settingsChanged_ && now - settingsChangedMs_ > 2000) {
      settingsChanged_ = false;
      if (onSettingsChanged) onSettingsChanged(ctx_.cfg, dark_);
    }
    app_.update();
  }

  /// Minimum time between two redraws caused by new data.
  void setRefreshIntervalMs(uint32_t ms) { refreshIntervalMs_ = ms; }

  void showPage(int index) {
    if (!built_) return;
    hideWifi();
    navigator_.show(index);
  }

  /// Shows the WiFi page (if setWifi() was called).
  void showWifi() {
    if (!built_ || wifi_ == nullptr) return;
    keyboard_.hide();
    for (int i = 0; i < navigator_.count(); ++i) navigator_.page(i).visible = false;
    nav_.clearSelection();  // a tap on any tab leaves the WiFi page
    wifiPage_.visible = true;
    wifiVisible_ = true;
    wifiPage_.onShow();
    forceRefresh_ = true;
    app_.screen().invalidate();
  }

  bool isWifiShown() const { return wifiVisible_; }

  bool isDark() const { return dark_; }
  void setDark(bool dark) {
    dark_ = dark;
    app_.screen().setTheme(dark ? darkTheme_ : lightTheme_);
    forceRefresh_ = true;
  }

  tinymd::App<RGB_T>& app() { return app_; }
  PageContext<RGB_T>& context() { return ctx_; }
  ClockPage<RGB_T>& clockPage() { return clock_; }
  SourcesPage<RGB_T>& sourcesPage() { return sources_; }
  PublishPage<RGB_T>& publishPage() { return publish_; }
  MetricsPage<RGB_T>& metricsPage() { return metrics_; }
  SettingsPage<RGB_T>& settingsPage() { return settings_; }
  tinymd::WifiLoginPage<RGB_T>& wifiPage() { return wifiPage_; }

 private:
  static constexpr int32_t kBarHeight = 40;
  static constexpr int32_t kNavHeight = 56;
  static constexpr int32_t kKeyboardHeight = 170;

  tinymd::MaterialTheme<RGB_T> darkTheme_;
  tinymd::MaterialTheme<RGB_T> lightTheme_;
  tinymd::App<RGB_T> app_;
  tinymd::Keyboard<RGB_T> keyboard_;
  PageContext<RGB_T> ctx_;
  ClockPage<RGB_T> clock_;
  SourcesPage<RGB_T> sources_;
  PublishPage<RGB_T> publish_;
  MetricsPage<RGB_T> metrics_;
  SettingsPage<RGB_T> settings_;
  std::vector<ServicePage<RGB_T>*> pages_;
  tinymd::NavigationBar<RGB_T> nav_;
  tinymd::PageNavigator<RGB_T> navigator_;
  tinymd::WifiLoginPage<RGB_T> wifiPage_;
  tinymd::WifiControl* wifi_ = nullptr;
  bool wifiVisible_ = false;
  bool wifiChecked_ = false;
  uint32_t wifiTimeoutMs_ = 15000;
  uint32_t beginMs_ = 0;

  // frame
  tinymd::AppBar<RGB_T> appBar_;
  StatusDot<RGB_T> syncDot_;
  tinymd::IconButton<RGB_T> menuButton_;
  tinymd::Menu<RGB_T> menu_;
  tinymd::ListItem<RGB_T> wifiItem_, syncItem_, themeItem_, shutdownItem_;
  tinymd::Dialog<RGB_T> shutdownDialog_;
  tinymd::Button<RGB_T> shutdownCancel_, shutdownConfirm_;

  bool built_ = false;
  bool dark_ = true;
  bool forceRefresh_ = true;
  bool settingsChanged_ = false;
  uint32_t settingsChangedMs_ = 0;
  uint32_t refreshIntervalMs_ = 250;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastRevision_ = 0;

  int32_t width() const { return static_cast<int32_t>(app_.width()); }
  int32_t height() const { return static_cast<int32_t>(app_.height()); }

  void hideWifi() {
    if (!wifiVisible_) return;
    wifiVisible_ = false;
    wifiPage_.visible = false;
    wifiPage_.onHide();
  }

  /// Shows the WiFi page once on startup: at once without stored
  /// credentials, otherwise when they fail or do not connect in time.
  void checkWifiOnStartup(uint32_t now) {
    if (wifiChecked_ || wifi_ == nullptr) return;
    const tinymd::WifiControl::State state = wifi_->state();
    if (state == tinymd::WifiControl::State::kConnected) {
      wifiChecked_ = true;
      return;
    }
    const bool failed = state == tinymd::WifiControl::State::kFailed;
    const bool timeout = now - beginMs_ >= wifiTimeoutMs_;
    if (!wifi_->hasCredentials() || failed || timeout) {
      wifiChecked_ = true;
      showWifi();
    }
  }

  /// Applies the edited configuration and updates all pages.
  void configChanged() {
    ctx_.engine.setConfig(ctx_.cfg);
    for (ServicePage<RGB_T>* page : pages_) page->configChanged();
    settingsChanged_ = true;
    settingsChangedMs_ = millis();
    forceRefresh_ = true;
  }

  void buildFrame() {
    const int32_t w = width();
    appBar_ = tinymd::AppBar<RGB_T>(tinymd::Bounds(0, 0, w, kBarHeight), "Time Services");
    menuButton_ = tinymd::IconButton<RGB_T>(appBar_.trailingRect(), tinymd::drawMenu<RGB_T>);
    menuButton_.onClick = [this]() { openMenu(); };
    appBar_.trailing = &menuButton_;
    app_.screen().addFixedWidget(appBar_);
    const tinymd::Bounds tr = appBar_.trailingRect();
    syncDot_.bounds = tinymd::Bounds(tr.x - 14, kBarHeight / 2 - 5, 10, 10);
    app_.screen().addFixedWidget(syncDot_);
    buildMenu();
  }

  void buildMenu() {
    constexpr int32_t menuW = 160;
    constexpr int32_t itemH = 44;
    menu_ = tinymd::Menu<RGB_T>(tinymd::Bounds(width() - menuW - 4, kBarHeight, menuW, 4 * itemH));
    wifiItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(0, itemH), "WiFi", tinymd::drawWifi<RGB_T>);
    wifiItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      showWifi();
    };
    menu_.addItem(wifiItem_);
    syncItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(0, itemH), "Sync now", drawSyncIcon<RGB_T>);
    syncItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      ctx_.engine.forceSync();
      forceRefresh_ = true;
    };
    menu_.addItem(syncItem_);
    themeItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(1, itemH), "Light theme", drawThemeIcon<RGB_T>);
    themeItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      setDark(!dark_);
      configChanged();
    };
    menu_.addItem(themeItem_);
    shutdownItem_ = tinymd::ListItem<RGB_T>(menu_.itemRect(2, itemH), "Shut down", drawPowerIcon<RGB_T>);
    shutdownItem_.onClick = [this]() {
      app_.screen().dismissDialog();
      app_.screen().presentDialog(shutdownDialog_);
    };
    menu_.addItem(shutdownItem_);
    menu_.onOutsideTap = [this]() { app_.screen().dismissDialog(); };

    const int32_t dialogH = 150;
    shutdownDialog_ =
        tinymd::Dialog<RGB_T>(tinymd::Bounds(16, (height() - dialogH) / 2, width() - 32, dialogH), "Shut down?",
                              "The time services are stopped and the device is switched off.");
    shutdownCancel_ = tinymd::Button<RGB_T>(shutdownDialog_.actionRect(0, 2), "Cancel", tinymd::ButtonVariant::kText);
    shutdownCancel_.onClick = [this]() { app_.screen().dismissDialog(); };
    shutdownConfirm_ = tinymd::Button<RGB_T>(shutdownDialog_.actionRect(1, 2), "Shut down");
    shutdownConfirm_.onClick = [this]() {
      app_.screen().dismissDialog();
      if (settingsChanged_) {
        settingsChanged_ = false;
        if (onSettingsChanged) onSettingsChanged(ctx_.cfg, dark_);
      }
      if (onShutdown) onShutdown();
    };
    shutdownDialog_.addAction(shutdownCancel_);
    shutdownDialog_.addAction(shutdownConfirm_);
  }

  void openMenu() {
    constexpr int32_t itemH = 44;
    themeItem_.setTitle(dark_ ? "Light theme" : "Dark theme");
    wifiItem_.visible = wifi_ != nullptr;
    shutdownItem_.visible = static_cast<bool>(onShutdown);
    // the visible items from the top
    int count = 0;
    for (tinymd::ListItem<RGB_T>* item : {&wifiItem_, &syncItem_, &themeItem_, &shutdownItem_}) {
      if (item->visible) item->bounds = menu_.itemRect(count++, itemH);
    }
    menu_.bounds.h = count * itemH;
    app_.screen().presentDialog(menu_);
  }
};

}  // namespace time_services

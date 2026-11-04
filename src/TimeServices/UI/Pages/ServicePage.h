#pragma once
#include <functional>

#include "TimeServices/TimeEngine.h"
#include "TimeServices/UI/Theme.h"
#include "TimeServices/UI/Widgets.h"
#include "TinyMaterialDesign.h"

namespace time_services {

/**
 * @brief What the pages share: the engine, the configuration that is edited,
 * the on-screen keyboard and the actions of the app. Owned by
 * TimeServicesApp.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
struct PageContext {
  PageContext(TimeEngine& engine, tinymd::App<RGB_T>& app, tinymd::Keyboard<RGB_T>& keyboard)
      : engine(engine), app(app), keyboard(keyboard) {}

  TimeEngine& engine;
  tinymd::App<RGB_T>& app;
  tinymd::Keyboard<RGB_T>& keyboard;
  /// The configuration as edited by the GUI (applied with configChanged()).
  SystemConfig cfg;
  Accents<RGB_T> accents;
  /// Area of the pages (between app bar and navigation bar).
  int32_t top = 0;
  int32_t height = 0;

  /// Applies cfg to the engine and lets all pages update.
  std::function<void()> onConfigChanged;
  std::function<void(int page)> showPage;
  std::function<void(bool dark)> setDark;
  std::function<bool()> isDark;
  /// Requests a refresh of the page with new data.
  std::function<void()> requestRefresh;

  void configChanged() {
    if (onConfigChanged) onConfigChanged();
  }
  void refreshSoon() {
    if (requestRefresh) requestRefresh();
  }
};

/**
 * @brief Base class of the tabs: a tinymd::Page (a scrollable Container with
 * title and icon, shown by a tinymd::PageNavigator) that builds its own
 * widgets and updates them from the TimeEngine.
 *
 * The navigator calls build() once (after the page was added to the screen,
 * so it has a theme), tick() regularly while it is shown; the app calls
 * refresh() when new data is available while the page is shown and
 * configChanged() when the configuration was changed by any page.
 */
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
class ServicePage : public tinymd::Page<RGB_T> {
 public:
  ServicePage(PageContext<RGB_T>& ctx, const char* title, tinymd::IconPainter<RGB_T> icon)
      : tinymd::Page<RGB_T>(title, icon), ctx_(ctx) {}

  /// Updates the widgets from the engine.
  virtual void refresh() {}
  /// The configuration (ctx.cfg) was changed.
  virtual void configChanged() {}

 protected:
  static constexpr int32_t kMargin = 8;
  static constexpr int32_t kRightMargin = tinymd::Container<RGB_T>::kScrollbarTouchWidth;

  PageContext<RGB_T>& ctx_;

  TimeEngine& engine() { return ctx_.engine; }
  SystemConfig& cfg() { return ctx_.cfg; }
  const Accents<RGB_T>& accents() const { return ctx_.accents; }
  const tinymd::MaterialTheme<RGB_T>& theme() const { return ctx_.app.theme(); }
  int32_t width() const { return static_cast<int32_t>(ctx_.app.width()); }
  int32_t screenHeight() const { return static_cast<int32_t>(ctx_.app.height()); }
  /// Width of the content (keeps the scrollbar free).
  int32_t inner() const { return width() - kMargin - kRightMargin; }
  int32_t pageTop() const { return ctx_.top; }

  /// Positions widgets from top to bottom; hidden ones get empty bounds.
  struct Placer {
    int32_t x, y, w, top;
    void place(tinymd::Widget<RGB_T>& widget, int32_t h, bool show = true, int32_t gap = 6) {
      widget.visible = show;
      if (!show) {
        widget.bounds = tinymd::Bounds(x, top, 0, 0);  // still counts for the content height
        return;
      }
      widget.bounds = tinymd::Bounds(x, y, w, h);
      y += h + gap;
    }
  };

  /// Placer over the whole width of the page, starting at its top.
  Placer pagePlacer(int32_t gap = 6) const { return Placer{kMargin, pageTop() + gap, inner(), pageTop()}; }
  /// Placer inside a card that starts at y.
  Placer cardPlacer(int32_t y) const { return Placer{kMargin + 8, y + 8, inner() - 16, pageTop()}; }

  void add(tinymd::Widget<RGB_T>& widget) { this->addChild(widget); }
};

}  // namespace time_services

#pragma once
#include "TinyMaterialDesign.h"

namespace time_services {

/// Clock face with hands (Clock tab).
template <typename RGB_T>
void drawClockIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX(), cy = r.centerY();
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 0.0f, 360.0f, color, thickness);
  tinymd::drawLineThick(target, cx, cy, cx, cy - radius * 2 / 3, color, thickness);
  tinymd::drawLineThick(target, cx, cy, cx + radius / 2, cy, color, thickness);
}

/// Dot with radio waves (Sources tab).
template <typename RGB_T>
void drawSensorsIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t cx = r.centerX(), cy = r.centerY();
  target.fillCircle(toPx(cx), toPx(cy), 2, color);
  const int32_t r1 = r.w / 4, r2 = r.w * 2 / 5;
  target.drawArc(toPx(cx), toPx(cy), toPx(r1), 135.0f, 225.0f, color, thickness);
  target.drawArc(toPx(cx), toPx(cy), toPx(r1), 315.0f, 405.0f, color, thickness);
  target.drawArc(toPx(cx), toPx(cy), toPx(r2), 135.0f, 225.0f, color, thickness);
  target.drawArc(toPx(cx), toPx(cy), toPx(r2), 315.0f, 405.0f, color, thickness);
}

/// Antenna mast with waves (Publish tab).
template <typename RGB_T>
void drawTowerIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t cx = r.centerX();
  const int32_t top = r.y + r.h / 3, bottom = r.bottom() - 2;
  tinymd::drawLineThick(target, cx, top, cx - r.w / 4, bottom, color, thickness);
  tinymd::drawLineThick(target, cx, top, cx + r.w / 4, bottom, color, thickness);
  target.fillCircle(toPx(cx), toPx(top), 2, color);
  target.drawArc(toPx(cx), toPx(top), toPx(r.w / 4), 220.0f, 320.0f, color, thickness);
  target.drawArc(toPx(cx), toPx(top), toPx(r.w * 2 / 5), 220.0f, 320.0f, color, thickness);
}

/// Zig-zag line (Metrics tab).
template <typename RGB_T>
void drawChartIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  const tinymd::Bounds in = r.inset(r.w / 6);
  const int32_t xs[] = {in.x, in.x + in.w / 3, in.x + in.w * 2 / 3, in.right()};
  const int32_t ys[] = {in.bottom(), in.y + in.h / 3, in.y + in.h * 2 / 3, in.y};
  for (int i = 0; i < 3; ++i) tinymd::drawLineThick(target, xs[i], ys[i], xs[i + 1], ys[i + 1], color, thickness);
}

/// Three slider lines with knobs (Settings tab).
template <typename RGB_T>
void drawTuneIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const tinymd::Bounds in = r.inset(r.w / 6);
  const int32_t knobs[] = {in.w * 2 / 3, in.w / 4, in.w / 2};
  for (int i = 0; i < 3; ++i) {
    const int32_t y = in.y + 1 + i * (in.h - 2) / 2;
    tinymd::drawLineThick(target, in.x, y, in.right(), y, color, thickness);
    target.fillCircle(toPx(in.x + knobs[i]), toPx(y), 3, color);
  }
}

/// Two arrows in a circle (Sync now).
template <typename RGB_T>
void drawSyncIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX(), cy = r.centerY();
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 200.0f, 340.0f, color, thickness);
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 20.0f, 160.0f, color, thickness);
  tinymd::drawLineThick(target, cx + radius, cy - 4, cx + radius, cy + 1, color, thickness);
  tinymd::drawLineThick(target, cx - radius, cy + 4, cx - radius, cy - 1, color, thickness);
}

/// Half filled circle (theme).
template <typename RGB_T>
void drawThemeIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX(), cy = r.centerY();
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 0.0f, 360.0f, color, thickness);
  for (int32_t dy = -radius; dy <= radius; ++dy) {
    const int32_t half = static_cast<int32_t>(sqrtf(static_cast<float>(radius * radius - dy * dy)));
    if (half > 0) target.fillRect(toPx(cx), toPx(cy + dy), toPx(half), 1, color);
  }
}

/// Open circle with a vertical bar (Shut down).
template <typename RGB_T>
void drawPowerIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX();
  const int32_t cy = r.centerY() + 1;
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 300.0f, 600.0f, color, thickness);
  tinymd::drawLineThick(target, cx, cy - radius - 1, cx, cy - 1, color, thickness);
}

/// Globe (NTP source).
template <typename RGB_T>
void drawGlobeIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 1) {
  using tinymd::toPx;
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX(), cy = r.centerY();
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 0.0f, 360.0f, color, thickness + 1);
  target.drawArc(toPx(cx - radius / 2), toPx(cy), toPx(radius), 300.0f, 420.0f, color, thickness);
  target.drawArc(toPx(cx + radius / 2), toPx(cy), toPx(radius), 120.0f, 240.0f, color, thickness);
  tinymd::drawLineThick(target, cx - radius, cy, cx + radius, cy, color, thickness);
}

/// Satellite (GNSS source).
template <typename RGB_T>
void drawSatelliteIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  using tinymd::toPx;
  const int32_t cx = r.centerX(), cy = r.centerY();
  const int32_t s = r.w / 8;
  target.fillRect(toPx(cx - s), toPx(cy - s), toPx(2 * s), toPx(2 * s), color);
  tinymd::drawLineThick(target, cx - 4 * s, cy - 2 * s, cx - 2 * s, cy - 2 * s, color, thickness);
  tinymd::drawLineThick(target, cx + 2 * s, cy + 2 * s, cx + 4 * s, cy + 2 * s, color, thickness);
  tinymd::drawLineThick(target, cx - s, cy - s, cx - 3 * s, cy - 3 * s, color, thickness);
  tinymd::drawLineThick(target, cx + s, cy + s, cx + 3 * s, cy + 3 * s, color, thickness);
}

/// Bluetooth rune (BLE CTS source).
template <typename RGB_T>
void drawBluetoothIcon(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& r, RGB_T color, uint8_t thickness = 2) {
  const int32_t cx = r.centerX();
  const int32_t top = r.y + r.h / 6, bottom = r.bottom() - r.h / 6;
  const int32_t q = (bottom - top) / 4;
  const int32_t right = cx + r.w / 5, left = cx - r.w / 5;
  tinymd::drawLineThick(target, cx, top, cx, bottom, color, thickness);
  tinymd::drawLineThick(target, cx, top, right, top + q, color, thickness);
  tinymd::drawLineThick(target, right, top + q, left, bottom - q, color, thickness);
  tinymd::drawLineThick(target, cx, bottom, right, bottom - q, color, thickness);
  tinymd::drawLineThick(target, right, bottom - q, left, top + q, color, thickness);
}

}  // namespace time_services

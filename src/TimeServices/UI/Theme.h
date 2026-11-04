#pragma once
#include <math.h>
#include <stdio.h>

#include "TimeServices/Models.h"
#include "TinyMaterialDesign.h"

namespace time_services {

/// Accent colors (the same as the Android app).
template <typename RGB_T>
struct Accents {
  RGB_T cyan = tinymd::colorFromHex<RGB_T>(0x06B6D4);
  RGB_T emerald = tinymd::colorFromHex<RGB_T>(0x10B981);
  RGB_T amber = tinymd::colorFromHex<RGB_T>(0xF59E0B);
  RGB_T purple = tinymd::colorFromHex<RGB_T>(0xA78BFA);
  RGB_T blue = tinymd::colorFromHex<RGB_T>(0x3B82F6);
  RGB_T rose = tinymd::colorFromHex<RGB_T>(0xF43F5E);
  RGB_T slate = tinymd::colorFromHex<RGB_T>(0x64748B);
};

/// Dark slate / cyan theme of the Android app.
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
tinymd::MaterialTheme<RGB_T> timeServicesDarkTheme() {
  using tinymd::colorFromHex;
  tinymd::MaterialTheme<RGB_T> theme = tinymd::defaultDarkTheme<RGB_T>();
  tinymd::ColorScheme<RGB_T>& c = theme.colors;
  c.primary = colorFromHex<RGB_T>(0x06B6D4);
  c.onPrimary = colorFromHex<RGB_T>(0x020617);
  c.primaryContainer = colorFromHex<RGB_T>(0x155E75);
  c.onPrimaryContainer = colorFromHex<RGB_T>(0xCFFAFE);
  c.secondary = colorFromHex<RGB_T>(0x10B981);
  c.onSecondary = colorFromHex<RGB_T>(0x020617);
  c.secondaryContainer = colorFromHex<RGB_T>(0x155E75);
  c.onSecondaryContainer = colorFromHex<RGB_T>(0xCFFAFE);
  c.error = colorFromHex<RGB_T>(0xF43F5E);
  c.onError = colorFromHex<RGB_T>(0xFFFFFF);
  c.background = colorFromHex<RGB_T>(0x020617);
  c.onBackground = colorFromHex<RGB_T>(0xF1F5F9);
  c.surface = colorFromHex<RGB_T>(0x0F172A);
  c.onSurface = colorFromHex<RGB_T>(0xF1F5F9);
  c.surfaceVariant = colorFromHex<RGB_T>(0x1E293B);
  c.onSurfaceVariant = colorFromHex<RGB_T>(0x94A3B8);
  c.outline = colorFromHex<RGB_T>(0x334155);
  return theme;
}

/// Light slate / cyan theme of the Android app.
template <typename RGB_T = TINYMD_DEFAULT_RGB_T>
tinymd::MaterialTheme<RGB_T> timeServicesLightTheme() {
  using tinymd::colorFromHex;
  tinymd::MaterialTheme<RGB_T> theme = tinymd::defaultTheme<RGB_T>();
  tinymd::ColorScheme<RGB_T>& c = theme.colors;
  c.primary = colorFromHex<RGB_T>(0x0891B2);
  c.onPrimary = colorFromHex<RGB_T>(0xFFFFFF);
  c.primaryContainer = colorFromHex<RGB_T>(0xCFFAFE);
  c.onPrimaryContainer = colorFromHex<RGB_T>(0x164E63);
  c.secondary = colorFromHex<RGB_T>(0x059669);
  c.onSecondary = colorFromHex<RGB_T>(0xFFFFFF);
  c.secondaryContainer = colorFromHex<RGB_T>(0xCFFAFE);
  c.onSecondaryContainer = colorFromHex<RGB_T>(0x164E63);
  c.error = colorFromHex<RGB_T>(0xE11D48);
  c.onError = colorFromHex<RGB_T>(0xFFFFFF);
  c.background = colorFromHex<RGB_T>(0xF8FAFC);
  c.onBackground = colorFromHex<RGB_T>(0x0F172A);
  c.surface = colorFromHex<RGB_T>(0xFFFFFF);
  c.onSurface = colorFromHex<RGB_T>(0x0F172A);
  c.surfaceVariant = colorFromHex<RGB_T>(0xE2E8F0);
  c.onSurfaceVariant = colorFromHex<RGB_T>(0x475569);
  c.outline = colorFromHex<RGB_T>(0xCBD5E1);
  return theme;
}

/// Color of a source status (as in the Android app).
template <typename RGB_T>
RGB_T statusColor(SourceStatus s) {
  const Accents<RGB_T> a;
  switch (s) {
    case SourceStatus::kLocked:
      return a.emerald;
    case SourceStatus::kCandidate:
      return a.cyan;
    case SourceStatus::kOutlier:
      return a.amber;
    case SourceStatus::kError:
      return a.rose;
    default:
      return a.slate;
  }
}

/// Draws the rounded card background; `accent` colors the border.
template <typename RGB_T>
void drawCardFrame(tinygpu::ISurface<RGB_T>& target, const tinymd::Bounds& b, const tinymd::MaterialTheme<RGB_T>& theme,
                   const RGB_T* accent = nullptr) {
  using tinymd::toPx;
  const size_t radius = toPx(theme.shape.large);
  target.fillRoundRect(toPx(b.x), toPx(b.y), toPx(b.w), toPx(b.h), radius, theme.colors.surface);
  target.drawRoundRect(toPx(b.x), toPx(b.y), toPx(b.w), toPx(b.h), radius,
                       accent != nullptr ? *accent : theme.colors.outline);
}

/// Draws `text` with the indicated font.
template <typename RGB_T>
void drawText(tinygpu::ISurface<RGB_T>& target, tinygpu::IFont<RGB_T>& font, int32_t x, int32_t y, const char* text,
              RGB_T color, RGB_T background) {
  font.drawText(target, static_cast<int16_t>(x), static_cast<int16_t>(y), text, color, background, false);
}

template <typename RGB_T>
int32_t textWidth(tinygpu::IFont<RGB_T>& font, const char* text) {
  return static_cast<int32_t>(font.measureTextWidth(text));
}

template <typename RGB_T>
int32_t textHeight(tinygpu::IFont<RGB_T>& font) {
  return static_cast<int32_t>(font.getHeight(1));
}

}  // namespace time_services

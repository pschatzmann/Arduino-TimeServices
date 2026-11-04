#pragma once
#include <Arduino.h>
#include <stdint.h>

// gettimeofday()/settimeofday() where the platform has them (ESP32, RP2040,
// the desktop); otherwise the clock is millis() based
#if defined(__has_include)
#if __has_include(<sys/time.h>) && !defined(ARDUINO_ARCH_RENESAS) && !defined(ARDUINO_ARCH_AVR)
#include <sys/time.h>
#define TS_HAS_SYSTEM_TIME 1
#endif
#endif

namespace time_services {

/**
 * @brief The clock of the device that is disciplined: the offsets of the
 * time sources are measured against it. Override it to use e.g. an RTC.
 */
class LocalClock {
 public:
  virtual ~LocalClock() = default;
  /// The time of the clock in ms since 1970 (with fractions).
  virtual double nowMs() = 0;
  /// Sets the clock forward by deltaMs. Returns false if not supported.
  virtual bool step(double deltaMs) {
    (void)deltaMs;
    return false;
  }
};

/**
 * @brief The system clock (gettimeofday()), or millis() since the start on
 * boards without one. step() uses settimeofday(): on the desktop this needs
 * the rights to set the clock (it fails otherwise).
 */
class SystemClock : public LocalClock {
 public:
  double nowMs() override {
#ifdef TS_HAS_SYSTEM_TIME
    timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<double>(tv.tv_sec) * 1000.0 + tv.tv_usec / 1000.0;
#else
    // 64 bit micros(): handles the overflow of the 32 bit counter
    const uint32_t now = micros();
    highMicros_ += static_cast<uint32_t>(now - lastMicros_);
    lastMicros_ = now;
    return baseMs_ + highMicros_ / 1000.0;
#endif
  }

  bool step(double deltaMs) override {
#ifdef TS_HAS_SYSTEM_TIME
    const double target = nowMs() + deltaMs;
    timeval tv;
    tv.tv_sec = static_cast<time_t>(target / 1000.0);
    tv.tv_usec = static_cast<suseconds_t>((target - tv.tv_sec * 1000.0) * 1000.0);
    return settimeofday(&tv, nullptr) == 0;
#else
    baseMs_ += deltaMs;
    return true;
#endif
  }

 private:
#ifndef TS_HAS_SYSTEM_TIME
  double baseMs_ = 0;
  uint64_t highMicros_ = 0;
  uint32_t lastMicros_ = 0;
#endif
};

}  // namespace time_services

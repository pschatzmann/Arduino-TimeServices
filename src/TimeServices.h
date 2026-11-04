#pragma once
/**
 * @file TimeServices.h
 * @brief Header-only time services for Arduino (port of the Android app
 * Android-Time-Services): gathers the time from NTP servers (TinyNTP), a GNSS
 * receiver and Bluetooth LE peers (TinyCTS), fuses it into one clock
 * estimate (TimeEngine) and serves that time over NTP and the BLE Current
 * Time Service, with a TinyMaterialDesign touch GUI (TimeServicesApp).
 *
 * The Bluetooth classes are in TimeServices/BleCts.h: include it after
 * TinyCTS.h if you use them. The WiFi page is TinyMaterialDesign's
 * WifiLoginPage; on the ESP32 include TinyMaterialDesign/Wifi/WifiEsp32.h.
 */
#include <Arduino.h>

#include "TinyNTP.h"
#include "TimeServices/TimeServicesConfig.h"
#include "TimeServices/Models.h"
#include "TimeServices/LocalClock.h"
#include "TimeServices/TimeZoneInfo.h"
#include "TimeServices/TimeEngine.h"
#include "TimeServices/NtpSources.h"
#include "TimeServices/NtpPublisher.h"
#include "TimeServices/GnssSource.h"

#include "TimeServices/UI/Theme.h"
#include "TimeServices/UI/Icons.h"
#include "TimeServices/UI/Widgets.h"
#include "TimeServices/UI/TimeServicesApp.h"

#if defined(ARDUINO) || defined(TS_AUTO_NAMESPACE)
using namespace time_services;
#endif

#ifndef RTP_MIDI_CONFIG_H
#define RTP_MIDI_CONFIG_H

#ifndef ENABLE_WIFI_DEBUG
#define ENABLE_WIFI_DEBUG 0
#endif

// RTP-MIDI (Apple MIDI) is enabled by default. WiFi credentials are saved via the setup AP (see docs/build.md).
// OTA firmware updates over WiFi are enabled when RTP is on (ENABLE_OTA, default 1).
// Optional compile-time fallback: copy wifi_secrets.example.h to wifi_secrets.h.
#define ENABLE_RTP_MIDI 1
#define ENABLE_OTA 1

// MIDI clock can be extremely chatty. Keep it filtered for v1 hub mode unless a
// timing-sensitive setup explicitly opts in at compile time.
#ifndef ENABLE_MIDI_CLOCK_PASSTHROUGH
#define ENABLE_MIDI_CLOCK_PASSTHROUGH 0
#endif

#if ENABLE_RTP_MIDI
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#ifndef WIFI_SSID_TEXT
#define WIFI_SSID_TEXT ""
#endif
#ifndef WIFI_PASSWORD_TEXT
#define WIFI_PASSWORD_TEXT ""
#endif
#endif

#endif

#include "WifiDebugLog.h"

#if ENABLE_WIFI_DEBUG

#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

constexpr uint16_t kLogPort = 3333;
WiFiUDP logUdp;
char logLine[256];

}  // namespace

void wifiDebugLogBegin()
{
    logUdp.begin(kLogPort);
}

void wifiDebugLogPrintf(const char* fmt, ...)
{
    if (WiFi.status() != WL_CONNECTED || fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(logLine, sizeof(logLine), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }

    logUdp.beginPacket(IPAddress(255, 255, 255, 255), kLogPort);
    logUdp.write(reinterpret_cast<const uint8_t*>(logLine), static_cast<size_t>(n));
    logUdp.endPacket();
}

void wifiDebugLogPrintln(const char* msg)
{
    if (msg == nullptr) {
        return;
    }
    wifiDebugLogPrintf("%s\n", msg);
}

#else

void wifiDebugLogBegin() {}
void wifiDebugLogPrintf(const char* /*fmt*/, ...) {}
void wifiDebugLogPrintln(const char* /*msg*/) {}

#endif

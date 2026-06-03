#ifndef WIFI_DEBUG_LOG_H
#define WIFI_DEBUG_LOG_H

#include "../config/NetworkConfig.h"

#ifndef ENABLE_WIFI_DEBUG
#define ENABLE_WIFI_DEBUG 0
#endif

void wifiDebugLoggerBegin();
void wifiDebugLoggerPrintf(const char* fmt, ...);
void wifiDebugLoggerPrintln(const char* msg);

#endif

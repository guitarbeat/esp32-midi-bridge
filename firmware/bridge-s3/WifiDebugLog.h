#ifndef WIFI_DEBUG_LOG_H
#define WIFI_DEBUG_LOG_H

#include "RTPMidiConfig.h"

#ifndef ENABLE_WIFI_DEBUG
#define ENABLE_WIFI_DEBUG 0
#endif

void wifiDebugLogBegin();
void wifiDebugLogPrintf(const char* fmt, ...);
void wifiDebugLogPrintln(const char* msg);

#endif

#ifndef BRIDGE_LOG_H
#define BRIDGE_LOG_H

#include <Arduino.h>
#include "WifiDebugLog.h"

#define BRIDGE_LOG(...) \
    do { \
        Serial.printf(__VA_ARGS__); \
        wifiDebugLogPrintf(__VA_ARGS__); \
    } while (0)

#define BRIDGE_LOG_LN(msg) \
    do { \
        Serial.println(msg); \
        wifiDebugLogPrintln(msg); \
    } while (0)

#endif

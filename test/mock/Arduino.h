#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 0x1
#define LOW  0x0

#define INPUT 0x01
#define OUTPUT 0x02
#define INPUT_PULLUP 0x04

inline unsigned long millis() { return 0; }

#endif

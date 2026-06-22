#pragma once

#include <Arduino.h>
#include <SD.h>
#include <RTClib.h>
#include "config.h"

// ================== EXTERN GLOBALS ==================
extern bool              sd_ok;
extern bool              rtc_ok;
extern RTC_DS1307        rtc;
extern SemaphoreHandle_t sdMutex;

// ================== DECLARATIONS ==================
String getTimestamp();
String getDateStr();
void   sdLog(const char* level, const char* tag, const char* msg);
void   sdLog(const char* level, const char* tag, const String& msg);
void   ensureSDSpace();
String deleteOldestLog();
void   _logPrint(const char* tag, const char* fmt, ...);

// ================== LOG MACRO ==================
#define LOG(tag, fmt, ...) _logPrint(tag, fmt, ##__VA_ARGS__)

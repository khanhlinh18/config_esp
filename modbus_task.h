#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sd_log.h"

// ================== STRUCT ==================
struct SlaveGroup {
    uint8_t  id;
    uint16_t startReg;
    uint8_t  count;
    uint8_t  dataType;
    float    div;
    float    lastData[MAX_SLAVES];
    bool     isLost;
    bool     hasBeenRead;
};

// ================== EXTERN GLOBALS ==================
extern ModbusMaster       node;
extern Preferences        prefs;
extern SlaveGroup         myGroups[MAX_SLAVES];
extern uint8_t            totalGroups;
extern volatile bool      alarm_cloud;
extern String             alarm_message;
extern volatile bool      setup_mode;
extern SemaphoreHandle_t  modbusMutex;
extern SemaphoreHandle_t  configMutex;
extern SemaphoreHandle_t  dataMutex;

// ================== DECLARATIONS ==================
void preTransmission();
void postTransmission();
void writeRelayRS485(bool state);
void updateConfigFromJSON(const char* jsonStr);
void taskAlarm(void* pvParameters);
void taskModbus(void* pvParameters);

#pragma once

#ifndef TINY_GSM_MODEM_SIM7600
#define TINY_GSM_MODEM_SIM7600
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "sd_log.h"
#include "modbus_task.h"

// ================== EXTERN GLOBALS ==================
extern TinyGsm           modem;
extern TinyGsmClient     gsmClient;
extern WiFiClient        wifiClient;
extern PubSubClient      mqtt;
extern Preferences       prefs;
extern String            conf_ssid;
extern String            conf_pass;
extern String            conf_mqtt_server;
extern int               conf_mqtt_port;
extern bool              wifi_ok;
extern bool              gsm_ok;
extern bool              network_ok;
extern bool              mqtt_ok;
extern bool              is_modem_sleeping;
extern bool              ntp_synced;
extern bool              rtc_ok;
extern bool              rtc_synced;
extern String            auto_apn;
extern String            global_gsm_ip;
extern int               global_rssi;
extern volatile bool     ota_requested;
extern volatile bool     alarm_cloud;
extern String            alarm_message;
extern volatile bool     setup_mode;
extern SemaphoreHandle_t mqttMutex;
extern RTC_DS1307        rtc;

// ================== DECLARATIONS ==================
bool isInternetReachable();
void syncNTP();
void requestWiFiConnect();
void resetModem();
bool connectGSM();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void taskNetwork(void* pvParameters);

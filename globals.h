#ifndef GLOBALS_H
#define GLOBALS_H

#define TINY_GSM_MODEM_SIM7600
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <DNSServer.h>

// ================== PINS & HARDWARE ==================
#define SerialAT Serial2
#define MODEM_RX 27
#define MODEM_TX 26
#define MODEM_RST 25

#define MAX485_DE 4
#define MODBUS_SERIAL Serial1
#define RXD2 17
#define TXD2 16
#define RELAY_ALARM 12
#define SETUP_BUTTON 0 
#define LED_AP 13 

// ================== SYSTEM LIMITS ==================
#define MAX_SLAVES 15  
#define MAX_REGS_PER_SLAVE 5
#define ERR_LEN 16

extern const byte DNS_PORT;

// ================== OBJECTS ==================
extern TinyGsm modem;
extern TinyGsmClient gsmClient;
extern WiFiClient wifiClient;
extern PubSubClient mqtt;
extern ModbusMaster node;
extern Preferences prefs;
extern WebServer server;
extern DNSServer dnsServer;

// ================== GLOBAL VARIABLES ==================
extern String global_sim_ccid;
extern int global_rssi;
extern bool is_modem_sleeping;
extern String auto_apn;

extern String conf_ssid;
extern String conf_pass;
extern String conf_mqtt_server;
extern int conf_mqtt_port;
extern bool wifi_ok;     
extern bool gsm_ok;
extern bool network_ok;
extern bool mqtt_ok;

extern const char* www_user;
extern const char* www_pass;

extern volatile bool alarm_sensor;
extern volatile bool alarm_network;
extern volatile bool alarm_mqtt;

extern const char* TOPIC_DATA;
extern const char* TOPIC_CONFIG;
extern const char* TOPIC_ALARM;
extern const char* TOPIC_INFO;

extern bool setup_mode;
extern uint32_t setup_start_time;

extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t alarmMutex;
extern SemaphoreHandle_t mqttMutex;

// ================== MODBUS STRUCT  ==================
struct RegConfig {
  uint8_t type;
  float minVal;
  float maxVal;
  char err[ERR_LEN];
};

struct SlaveGroup {
  uint8_t id;
  uint16_t startReg;
  uint8_t count;      
  uint8_t dataType;   
  float div;          
  uint8_t regCount;   
  RegConfig regs[MAX_REGS_PER_SLAVE];
  float lastData[MAX_REGS_PER_SLAVE];
  bool alarmState[MAX_REGS_PER_SLAVE];
  bool isLost;
};

extern SlaveGroup myGroups[MAX_SLAVES];
extern uint8_t totalGroups;

// Web tasks / functions kept in main.cpp for now
void setupAPIEndpoints();
void startAP();
void taskWebServer(void *pvParameters);

#endif

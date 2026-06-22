#pragma once

// ================== FIRMWARE VERSION ==================
#define FW_VERSION "1.0.1"

// ================== MODEM ==================
#define TINY_GSM_MODEM_SIM7600

// ================== OTA CONFIG ==================
#define OTA_VERSION_URL_HTTPS  "https://github.com/khanhlinh18/esp32-OTA/releases/latest/download/version.json"
#define OTA_FIRMWARE_URL_HTTPS "https://github.com/khanhlinh18/esp32-OTA/releases/latest/download/OTA.ino.bin"

// ================== PINS & HARDWARE ==================
#define SerialAT      Serial2
#define MODEM_RX      27
#define MODEM_TX      26
#define MODEM_RST     25
#define MAX485_DE     4
#define MODBUS_SERIAL Serial1
#define RXD2          17
#define TXD2          16
#define SETUP_BUTTON  0
#define LED_AP        13

// ================== SD CARD (SPI) ==================
#define SD_CS   33
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// ================== RTC DS1307 (I2C) ==================
#define RTC_SDA 22
#define RTC_SCL 21

// ================== MODBUS ==================
#define MAX_SLAVES 15

// ================== SESSION ==================
#define SESSION_TIMEOUT_MS (30UL * 60UL * 1000UL)

// ================== SD SPACE ==================
#define SD_MIN_FREE_BYTES (512ULL * 1024ULL)

// ================== NTP ==================
#define NTP_SERVER1    "pool.ntp.org"
#define NTP_SERVER2    "time.google.com"
#define GMT_OFFSET_SEC (7 * 3600)
#define DST_OFFSET_SEC 0

// ================== MQTT TOPICS ==================
#define TOPIC_DATA   "factory/device10/data"
#define TOPIC_CONFIG "factory/device10/config"
#define TOPIC_ALARM  "factory/device10/alarm"
#define TOPIC_OTA    "factory/device10/ota"
#define TOPIC_INFO   "factory/device10/info"

// ================== AUTH ==================
#define WWW_USER   "admin"
#define MASTER_PIN "12345"

// ================== RELAY ==================
#define RELAY_MODBUS_ID 100
#define RELAY_COIL_ADDR 0x0000

#include "globals.h"
#include "web_config.h"

#include "modbus_task.h"
#include "mqtt_task.h"
#include "network_task.h"
#include "alarm_task.h"
#include "system_task.h"

// ================== SYSTEM LIMITS ==================
const byte DNS_PORT = 53;

// ================== OBJECTS ==================
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
WiFiClient wifiClient;
PubSubClient mqtt;
ModbusMaster node;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// ================== GLOBAL VARIABLES ==================
String global_sim_ccid = "Đang đọc...";
int global_rssi = 0;
bool is_modem_sleeping = true;
String auto_apn = "v-internet";

String conf_ssid = "";
String conf_pass = "";
String conf_mqtt_server = "";
int conf_mqtt_port = 1883;
bool wifi_ok = false;     
bool gsm_ok = false;
bool network_ok = false;
bool mqtt_ok = false;

const char* www_user = "admin";
const char* www_pass = "cuctac";

volatile bool alarm_sensor = false;
volatile bool alarm_network = false;
volatile bool alarm_mqtt = false;

const char* TOPIC_DATA = "factory/device01/data";
const char* TOPIC_CONFIG = "factory/device01/config";
const char* TOPIC_ALARM = "factory/device01/alarm";
const char* TOPIC_INFO = "factory/device01/info";

SlaveGroup myGroups[MAX_SLAVES];
uint8_t totalGroups = 0;

bool setup_mode = false;
uint32_t setup_start_time = 0;

SemaphoreHandle_t configMutex;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t alarmMutex;
SemaphoreHandle_t mqttMutex;

// ================== WEB TASKS ==================
void setupAPIEndpoints() {
  server.on("/api/info", HTTP_GET, []() {
    if (!server.authenticate(www_user, www_pass)) return server.requestAuthentication();
    JsonDocument doc;
    doc["mac"] = WiFi.macAddress();
    doc["ip"] = WiFi.localIP().toString();
    doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Disconnected";
    doc["sim_ccid"] = global_sim_ccid;
    
    if (is_modem_sleeping) {
        doc["rssi"] = WiFi.RSSI(); 
    } else {
        int ressi_raw = modem.getSignalQuality();
        doc["rssi"] = (ressi_raw != 99) ? (2 * ressi_raw) - 113 : -113;
    }
    
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/slaves", HTTP_GET, []() {
    if (!server.authenticate(www_user, www_pass)) return server.requestAuthentication();
    xSemaphoreTake(configMutex, portMAX_DELAY);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    JsonDocument doc;
    
    for (int i = 0; i < totalGroups; i++) {
        JsonObject sObj = doc["slaves"].add<JsonObject>();
        sObj["id"] = myGroups[i].id;
        sObj["start"] = myGroups[i].startReg;
        sObj["count"] = myGroups[i].count;
        sObj["dataType"] = myGroups[i].dataType;
        sObj["div"] = myGroups[i].div;
        
        JsonArray regsArr = sObj["regs"].to<JsonArray>();
        for (int j = 0; j < myGroups[i].regCount; j++) {
            JsonObject rObj = regsArr.add<JsonObject>();
            rObj["type"] = myGroups[i].regs[j].type;
            rObj["min"] = myGroups[i].regs[j].minVal;
            rObj["max"] = myGroups[i].regs[j].maxVal;
            rObj["err"] = myGroups[i].regs[j].err;
            // rObj["val"] = myGroups[i].lastData[j]; //  hiện data real-time
        }
    }
    xSemaphoreGive(configMutex);
    xSemaphoreGive(dataMutex);
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/save_slaves", HTTP_POST, []() {
    if (!server.authenticate(www_user, www_pass)) return server.requestAuthentication();
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        updateConfigFromJSON(body.c_str()); 
        server.send(200, "application/json", "{\"status\":\"OK\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_GET, []() {
    String q_ssid = server.arg("ssid");
    String q_pass = server.arg("pass");
    String q_mqtt_srv = server.arg("mqtt_srv");
    int q_mqtt_port = server.arg("mqtt_port").toInt();

    prefs.begin("net_cfg", false);
    prefs.putString("ssid", q_ssid);
    prefs.putString("pass", q_pass);
    prefs.putString("mqtt_srv", q_mqtt_srv);
    prefs.putInt("mqtt_port", q_mqtt_port);
    prefs.end();

    server.send(200, "text/html", "<h2 style='text-align:center; font-family:sans-serif; margin-top:50px;'>SAVED!<br>Gateway is restarting...</h2>");
    delay(2000);
    ESP.restart();
  });
}

void startAP() {
    pinMode(LED_AP, OUTPUT);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP("ESP_Setup", "")) {
        digitalWrite(LED_AP, LOW); // Bật LED báo hiệu
        Serial.println("[SYSTEM] Đã bật AP và LED báo hiệu.");
    }
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setup_start_time = millis(); // Ghi lại thời điểm bắt đầu
}

void taskWebServer(void *pvParameters) {
  setupAPIEndpoints();
  if (setup_mode) {
    startAP();
    Serial.println("[WEB] Đang chạy chế độ Setup (AP)");
  } else {
    while (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.printf("[WEB] Đang chạy chế độ Runtime (STA). IP: %s\n", WiFi.localIP().toString().c_str());
  }

  server.on("/", HTTP_GET, []() {
    if (setup_mode) {
      server.send(200, "text/html", htmlForm); // AP
    } else {
      if (!server.authenticate(www_user, www_pass)) {
        return server.requestAuthentication(); // STA 
      }
      server.send(200, "text/html", htmlDashboard); 
    }
  });

  server.onNotFound([]() {
    if (setup_mode) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "404: Not Found");
    }
  });

  server.begin();

  while (1) {
    if (setup_mode) dnsServer.processNextRequest(); 
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  esp_task_wdt_init(60, true);

  configMutex = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();
  alarmMutex = xSemaphoreCreateMutex();
  mqttMutex = xSemaphoreCreateMutex();
  
  pinMode(RELAY_ALARM, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);

  digitalWrite(RELAY_ALARM, HIGH);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(120);      
  mqtt.setSocketTimeout(30);

  getStaticSimInfo();
  loadConfig(); 

  if (conf_ssid == "") {
    Serial.println("[SYSTEM] Chưa có WiFi, tự động vào chế độ Setup..."); 
    setup_mode = true;
    setup_start_time = millis();
  } else {
    WiFi.mode(WIFI_STA);
    digitalWrite(LED_AP, HIGH);
    requestWiFiConnect();
  }

  MODBUS_SERIAL.begin(9600, SERIAL_8N1, RXD2, TXD2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  xTaskCreatePinnedToCore(taskWatchdog,    "Watchdog",2048, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(taskMQTTPublish, "MQTTPub", 6144, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskWebServer,   "Web",     4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskNetwork,     "Network", 8192, NULL, 3, NULL, 0);

  xTaskCreatePinnedToCore(taskButton,      "Button",  2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskModbus,      "Modbus",  4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskAlarm,       "Alarm",   4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(taskBuzzer,      "Buzzer",  2048, NULL, 2, NULL, 1);

  esp_task_wdt_delete(NULL); 
  Serial.println("[SYSTEM] Setup hoàn tất.");
}

void loop() {
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(1000));
}

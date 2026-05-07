#define TINY_GSM_MODEM_SIM7600
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <DNSServer.h>

#include "web_config.h"
#include "web_dashboard.h"
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
// ================== OBJECTS ==================
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
WiFiClient wifiClient;
PubSubClient mqtt;
ModbusMaster node;
Preferences prefs;
WebServer server(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;

String global_sim_ccid = "Đang đọc...";
int global_rssi = 0;
String global_gsm_ip = "0.0.0.0";
bool is_modem_sleeping = true;
String auto_apn = "v-internet";
// ================== CONFIG VARIABLES ==================
String conf_ssid = "";
String conf_pass = "";
String conf_mqtt_server = "";
int conf_mqtt_port = 1883;
bool wifi_ok = false;
bool gsm_ok = false;
bool network_ok = false;
bool mqtt_ok = false;

const char *www_user = "admin";
const char *www_pass = "cuctac";

volatile bool alarm_sensor = false;
volatile bool alarm_network = false;
volatile bool alarm_mqtt = false;

const char *TOPIC_DATA = "factory/device01/data";
const char *TOPIC_CONFIG = "factory/device01/config";
const char *TOPIC_ALARM = "factory/device01/alarm";
const char *TOPIC_INFO = "factory/device01/info";

// ================== MODBUS STRUCT  ==================

struct RegConfig
{
	uint8_t type;
	float minVal;
	float maxVal;
	char err[ERR_LEN];
};

struct SlaveGroup
{
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

SlaveGroup myGroups[MAX_SLAVES];
uint8_t totalGroups = 0;

bool setup_mode = false;
uint32_t setup_start_time = 0;

SemaphoreHandle_t configMutex;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t alarmMutex;
SemaphoreHandle_t mqttMutex;

// ================== RS485 ==================
void preTransmission() { digitalWrite(MAX485_DE, 1); }
void postTransmission() { digitalWrite(MAX485_DE, 0); }

// ================== ALARM BUZZER ==================
void taskBuzzer(void *pvParameters)
{
	while (1)
	{
		bool sensor, net, mqtt_err;

		xSemaphoreTake(alarmMutex, portMAX_DELAY);
		sensor = alarm_sensor;
		net = alarm_network;
		mqtt_err = alarm_mqtt;
		xSemaphoreGive(alarmMutex);

		if (sensor || net || mqtt_err)
		{
			digitalWrite(RELAY_ALARM, LOW);
			vTaskDelay(pdMS_TO_TICKS(1000));
			digitalWrite(RELAY_ALARM, HIGH);
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
		else
		{
			digitalWrite(RELAY_ALARM, HIGH);
			vTaskDelay(pdMS_TO_TICKS(500));
		}
	}
}

// ================== NVS & CONFIG LOGIC ==================
void updateConfigFromJSON(const char *jsonStr)
{
	JsonDocument doc;
	DeserializationError error = deserializeJson(doc, jsonStr);
	if (error) return;

	JsonArray slaves = doc["slaves"].as<JsonArray>();

	if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(5000)) == pdTRUE)
	{
		memset(myGroups, 0, sizeof(myGroups));
		int sSize = slaves.size();
		totalGroups = (sSize > MAX_SLAVES) ? MAX_SLAVES : sSize;

		for (int i = 0; i < totalGroups; i++)
		{
			JsonObject s = slaves[i];
			myGroups[i].id = s["id"] | 1;
			myGroups[i].startReg = s["start"] | 0;
			myGroups[i].count = s["count"] | 0;
			myGroups[i].dataType = s["dataType"] | 1;
			myGroups[i].div = s["div"] | 10.0;

			JsonArray regs = s["regs"].as<JsonArray>();
			int rSize = regs.size();
			myGroups[i].regCount = (rSize > MAX_REGS_PER_SLAVE) ? MAX_REGS_PER_SLAVE : rSize;

			for (int j = 0; j < myGroups[i].regCount; j++)
			{
				JsonObject r = regs[j];
				myGroups[i].regs[j].type = r["type"] | 0;
				myGroups[i].regs[j].minVal = r["min"] | 0.0;
				myGroups[i].regs[j].maxVal = r["max"] | 100.0;
				strlcpy(myGroups[i].regs[j].err, r["err"] | "ERR", ERR_LEN);
				myGroups[i].lastData[j] = -9999.0;
			}
		}
		xSemaphoreGive(configMutex);

		prefs.begin("modbus_cfg", false);
		prefs.putString("slave_cfg", jsonStr);
		prefs.end();
	}
}

void loadConfig()
{
	prefs.begin("net_cfg", false);
	conf_ssid = prefs.getString("ssid", "");
	conf_pass = prefs.getString("pass", "");
	conf_mqtt_server = prefs.getString("mqtt_srv", "broker.emqx.io");
	conf_mqtt_port = prefs.getInt("mqtt_port", 1883);
	prefs.end();

	prefs.begin("modbus_cfg", false);
	String savedSlavesJSON = prefs.getString("slave_cfg", "");
	prefs.end();

	if (savedSlavesJSON.length() > 10)
	{
		Serial.println("[SYSTEM] Tìm thấy cấu hình Slaves, đang khôi phục...");
		updateConfigFromJSON(savedSlavesJSON.c_str());
	} else {
		Serial.println("[SYSTEM] Chưa có cấu hình Slaves trong Flash.");
	}
}

// ================== ALARM TASK ==================
void taskAlarm(void *pvParameters)
{
	unsigned long last_send_time = 0;
	const uint32_t ALARM_INTERVAL = 5000;
	bool last_anySensorAlarm = false;

	while (1)
	{
		bool anySensorAlarm = false;
		char alarmList[1024] = "";
		int offset = 0;

		if (!setup_mode && totalGroups > 0)
		{
			xSemaphoreTake(configMutex, portMAX_DELAY);
			xSemaphoreTake(dataMutex, portMAX_DELAY);

			for (int i = 0; i < totalGroups; i++)
			{
				uint8_t id = myGroups[i].id;
				bool is_id_lost = false;
				char alarmMsg[128] = "";

				if (myGroups[i].isLost)
				{
					is_id_lost = true;
					anySensorAlarm = true;
					snprintf(alarmMsg, sizeof(alarmMsg), "[ID%d:LOST] ", id);

					for (int j = 0; j < myGroups[i].regCount; j++)
						myGroups[i].alarmState[j] = true;
				}

				if (!is_id_lost)
				{
					for (int j = 0; j < myGroups[i].regCount; j++)
					{
						float val = myGroups[i].lastData[j];
						bool currentItemAlarm = false;
						char itemMsg[64] = "";

						if (myGroups[i].regs[j].type == 0)
						{
							if (val > myGroups[i].regs[j].maxVal)
							{
								currentItemAlarm = true;
								snprintf(itemMsg, sizeof(itemMsg), "[ID%d:HI_%s] ", id, myGroups[i].regs[j].err);
							}
							else if (val < myGroups[i].regs[j].minVal)
							{
								currentItemAlarm = true;
								snprintf(itemMsg, sizeof(itemMsg), "[ID%d:LO_%s] ", id, myGroups[i].regs[j].err);
							}
						}
						else if (myGroups[i].regs[j].type == 1)
						{
							if (val < myGroups[i].regs[j].minVal)
							{
								currentItemAlarm = true;
								snprintf(itemMsg, sizeof(itemMsg), "[ID%d:%s] ", id, myGroups[i].regs[j].err);
							}
						}

						myGroups[i].alarmState[j] = currentItemAlarm;
						if (currentItemAlarm)
						{
							anySensorAlarm = true;
							strncat(alarmMsg, itemMsg, sizeof(alarmMsg) - strlen(alarmMsg) - 1);
						}
					}
				}

				if (strlen(alarmMsg) > 0 && offset + strlen(alarmMsg) < sizeof(alarmList))
				{
					strcat(alarmList, alarmMsg);
					offset += strlen(alarmMsg);
				}
			}
			xSemaphoreGive(configMutex);
			xSemaphoreGive(dataMutex);
		}

		if (mqtt_ok)
		{
			char publishPayload[1200] = "";
			bool shouldPublish = false;

			if (anySensorAlarm != last_anySensorAlarm)
			{
				if (anySensorAlarm)
				{
					snprintf(publishPayload, sizeof(publishPayload), "START ALARM: %s", alarmList);
				}
				else
				{
					snprintf(publishPayload, sizeof(publishPayload), "NORMAL");
				}
				shouldPublish = true;
			}
			else if (millis() - last_send_time > ALARM_INTERVAL)
			{
				if (anySensorAlarm)
				{
					snprintf(publishPayload, sizeof(publishPayload), "STILL ALARM: %s", alarmList);
				}
				else
				{
					snprintf(publishPayload, sizeof(publishPayload), "NORMAL");
				}
				shouldPublish = true;
			}

			if (shouldPublish)
			{
				if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(2000)) == pdTRUE)
				{
					mqtt.publish(TOPIC_ALARM, publishPayload);
					xSemaphoreGive(mqttMutex);
					last_send_time = millis();
				}
			}
		}

		last_anySensorAlarm = anySensorAlarm;

		xSemaphoreTake(alarmMutex, portMAX_DELAY);
		alarm_sensor = anySensorAlarm;
		xSemaphoreGive(alarmMutex);

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void setupAPIEndpoints()
{
  server.on("/api/info", HTTP_GET, []() {
    if (!server.authenticate(www_user, www_pass)) return server.requestAuthentication();
    JsonDocument doc;
    doc["mac"] = WiFi.macAddress();
    
    if (WiFi.status() == WL_CONNECTED) {
        doc["ip"] = WiFi.localIP().toString();
        doc["wifi"] = WiFi.SSID();
        doc["rssi"] = WiFi.RSSI();
    } else {
        doc["ip"] = global_gsm_ip;
        doc["wifi"] = "4G/GSM";
        doc["rssi"] = global_rssi; 
    }
    doc["sim_ccid"] = global_sim_ccid;
    
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response); 
  });

	server.on("/api/slaves", HTTP_GET, []()
			  {
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
    server.send(200, "application/json", response); });

	server.on("/api/save_slaves", HTTP_POST, []()
			  {
    if (!server.authenticate(www_user, www_pass)) return server.requestAuthentication();
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        updateConfigFromJSON(body.c_str()); 
        server.send(200, "application/json", "{\"status\":\"OK\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        server.send(400, "text/plain", "Bad Request");
    } });

	server.on("/scan", HTTP_GET, []()
			  {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json); });

	server.on("/save", HTTP_GET, []()
			  {
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
    ESP.restart(); });
}
// ================== WEB TASKS ==================
void startAP()
{
	pinMode(LED_AP, OUTPUT);
	WiFi.mode(WIFI_AP);
	if (WiFi.softAP("ESP_Setup", ""))
	{
		digitalWrite(LED_AP, LOW); // Bật LED báo hiệu
		Serial.println("[SYSTEM] Đã bật AP và LED báo hiệu.");
	}
	dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
	setup_start_time = millis(); // Ghi lại thời điểm bắt đầu
}
void taskWebServer(void *pvParameters)
{
	setupAPIEndpoints();
	if (setup_mode)
	{
		startAP();
		Serial.println("[WEB] Đang chạy chế độ Setup (AP)");
	}
	else
	{
		while (WiFi.status() != WL_CONNECTED)
		{
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
		Serial.printf("[WEB] Đang chạy chế độ Runtime (STA). IP: %s\n", WiFi.localIP().toString().c_str());
	}

	server.on("/", HTTP_GET, []()
			  {
    if (setup_mode) {
      server.send(200, "text/html", htmlForm); // AP
    } else {
      if (!server.authenticate(www_user, www_pass)) {
        return server.requestAuthentication(); // STA 
      }
      server.send(200, "text/html", htmlDashboard); 
    } });

	server.onNotFound([]()
					  {
    if (setup_mode) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "404: Not Found");
    } });

	server.begin();

	while (1)
	{
		if (setup_mode)
			dnsServer.processNextRequest();
		server.handleClient();
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

// ================== BUTTON TASK ==================
void taskButton(void *pvParameters)
{
	pinMode(SETUP_BUTTON, INPUT_PULLUP);
	uint32_t pressTime = 0;
	bool pressing = false;

	while (1)
	{
		if (digitalRead(SETUP_BUTTON) == LOW)
		{
			if (!pressing)
			{
				pressTime = millis();
				pressing = true;
			}
			// Nhấn giữ hơn 5 giây
			if (pressing && (millis() - pressTime > 5000))
			{
				Serial.println("\n[SYSTEM] ĐANG XÓA CẤU HÌNH VÀ REBOOT...");

				prefs.begin("net_cfg", false);
				prefs.clear(); // Xóa sạch SSID/PASS
				prefs.end();

				vTaskDelay(pdMS_TO_TICKS(500));
				ESP.restart();
			}
		}
		else
		{
			pressing = false;
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

// ================== MQTT LOGIC ==================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
	if (strcmp(topic, TOPIC_CONFIG) == 0)
	{
		if (length >= 4096)
			return;
		static char jsonStr[4096];
		memcpy(jsonStr, payload, length);
		jsonStr[length] = '\0';
		Serial.println("[MQTT] Nhận cấu hình mới!");
		updateConfigFromJSON(jsonStr);
	}
}

void requestWiFiConnect()
{
	static unsigned long last_attempt = 0;
	if (millis() - last_attempt < 20000)
		return;
	last_attempt = millis();

	Serial.println("[NET] Đang thử kết nối WiFi...");
	WiFi.begin(conf_ssid.c_str(), conf_pass.c_str());
}
void resetModem()
{
	pinMode(MODEM_RST, OUTPUT);
	digitalWrite(MODEM_RST, HIGH);
	vTaskDelay(pdMS_TO_TICKS(100));
	digitalWrite(MODEM_RST, LOW);
	vTaskDelay(pdMS_TO_TICKS(1000));
	digitalWrite(MODEM_RST, HIGH);
	for (int i = 0; i < 10; i++)
	{
		esp_task_wdt_reset();
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

bool connectGSM()
{
	Serial.println("=== GSM START ===");
	resetModem();
	modem.restart();

	esp_task_wdt_reset();

	if (!modem.init())
		return false;
	esp_task_wdt_reset();

	uint32_t start = millis();
	while (!modem.isNetworkConnected())
	{
		esp_task_wdt_reset();
		if (millis() - start > 60000)
			return false;
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	String imsi = modem.getIMSI();
	// String auto_apn = "v-internet";

	if (imsi.startsWith("45204"))
		auto_apn = "v-internet";
	else if (imsi.startsWith("45201"))
		auto_apn = "m-wap";
	else if (imsi.startsWith("45202"))
		auto_apn = "m3-world";
	else if (imsi.startsWith("45205"))
		auto_apn = "vietnamobile";
	else if (imsi.startsWith("45208"))
		auto_apn = "m9-itelecom";

	Serial.printf("[GSM] IMSI: %s -> Auto APN: %s\n", imsi.c_str(), auto_apn.c_str());

	if (!modem.gprsConnect(auto_apn.c_str(), "", ""))
		return false;
	if (!modem.isGprsConnected())
		return false;
	  Serial.println("[NET] GSM Connected!");
	  return true;
  }

void connectMQTT() {
  Serial.printf("[MQTT] Đang thử kết nối Server: %s:%d...\n", conf_mqtt_server.c_str(), conf_mqtt_port);
  mqtt.setServer(conf_mqtt_server.c_str(), conf_mqtt_port);
  mqtt.setCallback(mqttCallback);

  String clientId = "ESP32_" + WiFi.macAddress();
  clientId.replace(":", "");

  if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
    bool success = mqtt.connect(clientId.c_str());
    
    if (success) {
      Serial.println("[MQTT] Connected!");
      mqtt.subscribe(TOPIC_CONFIG);
    } else {
      Serial.printf("[MQTT] Thất bại, mã lỗi: %d\n", mqtt.state());
    }
    xSemaphoreGive(mqttMutex);
  }
}
// ================== NETWORK TASKS ==================
void taskNetwork(void *pvParameters){
	unsigned long wifi_start_time = millis();
	unsigned long last_wifi_recheck = millis();
	unsigned long last_mqtt_retry = 0;

	const unsigned long WIFI_WAIT_TIME = 15000;
	const unsigned long WIFI_RECHECK_INTERVAL = 600000;

	while (1)
	{
    if (setup_mode) { 
            vTaskDelay(pdMS_TO_TICKS(1000)); 
            continue; 
        }
		bool current_wifi_ok = (WiFi.status() == WL_CONNECTED);

		if (current_wifi_ok)
		{
			if (!is_modem_sleeping)
			{
				Serial.println("[NET] Đã có WiFi, tắt SIM...");
				modem.sendAT("+CPOWD=1");
				is_modem_sleeping = true;
				gsm_ok = false;
			}
			mqtt.setClient(wifiClient);
			last_wifi_recheck = millis();
		}
		else
		{
			if (!gsm_ok || is_modem_sleeping)
			{
				if (millis() - wifi_start_time < WIFI_WAIT_TIME)
				{
					requestWiFiConnect();
				}
				else
				{
					WiFi.disconnect();
					WiFi.mode(WIFI_OFF);
					vTaskDelay(pdMS_TO_TICKS(500));
					gsm_ok = connectGSM();
					if (gsm_ok)
					{
						is_modem_sleeping = false;
						mqtt.setClient(gsmClient);
						last_wifi_recheck = millis();
						last_mqtt_retry = 0; // Thử MQTT ngay khi GSM vừa lên
					}
					else
					{
						wifi_start_time = millis();
					}
				}
			}
			else
			{
				if (millis() - last_wifi_recheck > WIFI_RECHECK_INTERVAL)
				{
					Serial.println("[NET] Tạm dừng GSM để kiểm tra WiFi...");
					if (mqtt.connected())
					{
						Serial.println("[NET] Tạm dừng MQTT để quét WiFi...");
						mqtt.disconnect();
					}
					last_wifi_recheck = millis();

					WiFi.mode(WIFI_STA);
					requestWiFiConnect();

					unsigned long check_start = millis();
					bool found_wifi = false;
					while (millis() - check_start < 15000)
					{
						if (WiFi.status() == WL_CONNECTED)
						{
							found_wifi = true;
							break;
						}
						vTaskDelay(pdMS_TO_TICKS(500));
					}

					if (!found_wifi)
					{
						Serial.println("[NET] Vẫn không có WiFi, quay lại dùng GSM.");
						WiFi.mode(WIFI_OFF);
						// Cần dừng Client cũ để tránh treo Socket
						gsmClient.stop();
						// Kiểm tra lại GPRS
						if (!modem.isGprsConnected())
						{
							modem.gprsConnect(auto_apn.c_str(), "", "");
						}
						// Ép kết nối lại MQTT ngay vòng lặp sau
						last_mqtt_retry = 0;
					}
					last_wifi_recheck = millis();
				}
			}
		}
		// --- QUẢN LÝ KẾT NỐI MQTT ---
		if (current_wifi_ok || (gsm_ok && !is_modem_sleeping)){
      if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        mqtt_ok = mqtt.connected();
        xSemaphoreGive(mqttMutex);
      }
			if (!mqtt_ok){
				if (millis() - last_mqtt_retry > 10000 || last_mqtt_retry == 0){
          connectMQTT();
					last_mqtt_retry = millis();
				}
			} else {
				if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE){
					mqtt.loop();
					xSemaphoreGive(mqttMutex);
				}
			}
		} else {
      mqtt_ok = false;
    }
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

// ================== TASK MODBUS  ==================
void taskModbus(void *pvParameters)
{
	while (1)
	{
		if (!setup_mode && totalGroups > 0)
		{
			for (int i = 0; i < totalGroups; i++)
			{
				xSemaphoreTake(configMutex, portMAX_DELAY);
				uint8_t id = myGroups[i].id;
				uint16_t start = myGroups[i].startReg;
				uint8_t qty = myGroups[i].count;
				uint8_t dType = myGroups[i].dataType;
				float divisor = myGroups[i].div;
				// uint8_t rCount = myGroups[i].regCount;
				xSemaphoreGive(configMutex);

				node.begin(id, MODBUS_SERIAL);
				uint8_t result = node.readHoldingRegisters(start, qty);

				xSemaphoreTake(dataMutex, portMAX_DELAY);
				if (result == node.ku8MBSuccess)
				{
					myGroups[i].isLost = false;

					int bufferOffset = 0;
					for (int j = 0; j < myGroups[i].regCount; j++)
					{
						float rawVal = 0;

						if (dType == 2)
						{
							if (bufferOffset + 1 >= qty)
								break;
							uint32_t data = ((uint32_t)node.getResponseBuffer(bufferOffset) << 16) |
											node.getResponseBuffer(bufferOffset + 1);
							rawVal = (float)data;
							bufferOffset += 2;
						}
						else
						{
							rawVal = (float)node.getResponseBuffer(bufferOffset);
							bufferOffset += 1;
						}

						myGroups[i].lastData[j] = rawVal / divisor;
					}
				}
				else
				{
					myGroups[i].isLost = true;

					for (int j = 0; j < myGroups[i].regCount; j++)
					{
						myGroups[i].lastData[j] = -9999.0;
					}
				}
				xSemaphoreGive(dataMutex);
				vTaskDelay(pdMS_TO_TICKS(150));
			}
		}
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

void taskMQTTPublish(void *pvParameters) {
  static String last_ip = "";
  static String last_wifi = "";
  static String last_ccid = "";
  static int last_rssi = 0;
  static unsigned long last_data_send = 0;
  static unsigned long last_info_send = 0;

  const uint32_t DATA_INTERVAL = 30000;
  const uint32_t FORCE_SEND_INTERVAL = 15 * 60 * 1000;

  while (1) {
    if (!setup_mode) {
      String current_ip = "0.0.0.0";
      String current_wifi = "Disconnected";
      int current_rssi = -113;

      // 1. LẤY THÔNG TIN MẠNG (Độc lập, không bị ảnh hưởng bởi MQTT)
      if (WiFi.status() == WL_CONNECTED) {
        current_ip = WiFi.localIP().toString();
        current_wifi = WiFi.SSID();
        current_rssi = WiFi.RSSI();
      } 
      else if (gsm_ok) {
        current_wifi = "4G/GSM";
        if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          static unsigned long last_sim_check = 0;
          if (millis() - last_sim_check > 15000) { // Quét 15s/lần
            if (!is_modem_sleeping) {
              int rssi_raw = modem.getSignalQuality();
              global_rssi = (rssi_raw != 99) ? (2 * rssi_raw) - 113 : -113;
              global_gsm_ip = modem.localIP().toString();
            }
            last_sim_check = millis();
          }
          xSemaphoreGive(mqttMutex);
        }
        current_rssi = global_rssi;
        current_ip = global_gsm_ip;
      }

      // 2. GỬI TOPIC INFO (Chỉ xử lý khi mqtt_ok)
      if (mqtt_ok) {
        bool has_info_changed = false;
        if (current_ip != last_ip || current_wifi != last_wifi || abs(current_rssi - last_rssi) >= 5) {
          has_info_changed = true;
        }
        if (millis() - last_info_send > FORCE_SEND_INTERVAL) has_info_changed = true;

        if (has_info_changed) {
          JsonDocument infoDoc;
          infoDoc["mac"] = WiFi.macAddress();
          infoDoc["ip"] = current_ip;
          infoDoc["wifi"] = current_wifi;
          infoDoc["rssi"] = current_rssi;
          infoDoc["ccid"] = global_sim_ccid;

          char infoPayload[300];
          serializeJson(infoDoc, infoPayload);

          if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (mqtt.publish(TOPIC_INFO, infoPayload)) {
              Serial.printf("[MQTT] Info Updated -> RSSI: %d\n", current_rssi);
              last_ip = current_ip;
              last_wifi = current_wifi;
              last_rssi = current_rssi;
              last_info_send = millis();
            }
            xSemaphoreGive(mqttMutex);
          }
        }
      }

      // 3. GỬI DATA MODBUS (Chỉ xử lý khi mqtt_ok)
      if (mqtt_ok && totalGroups > 0 && (millis() - last_data_send > DATA_INTERVAL)) {
        xSemaphoreTake(dataMutex, portMAX_DELAY);

        for (int i = 0; i < totalGroups; i++) {
        char slaveTopic[100];
        char payload[1024]; 

        snprintf(slaveTopic, sizeof(slaveTopic), "factory/device01/slave%d", myGroups[i].id);

        int offset = 0;
        if (myGroups[i].isLost) {
            snprintf(payload, sizeof(payload), "{\"id%d\":\"val\":\"ERR\"}", myGroups[i].id);
        } else {
            offset = snprintf(payload, sizeof(payload), "{\"id%d\":[", myGroups[i].id);
            for (int j = 0; j < myGroups[i].regCount; j++) {
                if (j > 0) offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
                
                int regAddr = myGroups[i].startReg + (myGroups[i].dataType == 2 ? j * 2 : j);
                offset += snprintf(payload + offset, sizeof(payload) - offset, 
                                   "{\"reg%d\":%.1f}", regAddr, myGroups[i].lastData[j]);
            }
            snprintf(payload + offset, sizeof(payload) - offset, "]}");
        }

        if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            if (mqtt.publish(slaveTopic, payload)) {
                Serial.printf("[MQTT] Published: %s\n", slaveTopic);
            } else {
                Serial.printf("[MQTT] Publish FAILED to: %s\n", slaveTopic);
            }
            xSemaphoreGive(mqttMutex);
        }
          vTaskDelay(pdMS_TO_TICKS(150)); 
    }
    last_data_send = millis();
    xSemaphoreGive(dataMutex); 
  }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
// ================== TASK WATCHDOG ==================
void taskWatchdog(void *pvParameters)
{
	esp_task_wdt_add(NULL);
	const uint32_t MAX_OFFLINE_MS = 60 * 60 * 1000UL;
	const uint32_t MAINTENANCE_REBOOT = 7 * 24 * 60 * 60 * 1000UL;
	const uint32_t AP_TIMEOUT_MS = 3 * 60 * 1000UL;

	uint32_t last_connected_time = millis();
	uint32_t system_start_time = millis();

	while (1)
	{
		esp_task_wdt_reset();
		if (mqtt.connected())
			last_connected_time = millis();

		if (millis() - last_connected_time > MAX_OFFLINE_MS)
		{
			Serial.println("[WATCHDOG] Mất kết nối quá lâu. Reboot...");
			vTaskDelay(pdMS_TO_TICKS(500));
			ESP.restart();
		}

		if (millis() - system_start_time > MAINTENANCE_REBOOT)
		{
			Serial.println("[WATCHDOG] Reboot định kỳ...");
			vTaskDelay(pdMS_TO_TICKS(500));
			ESP.restart();
		}
		if (setup_mode && (millis() - setup_start_time > AP_TIMEOUT_MS))
		{
			Serial.println("[WATCHDOG] Hết thời gian Setup (3 phút). Tắt AP, chuyển qua 4G...");

			setup_mode = false;
			WiFi.softAPdisconnect(true);
			WiFi.mode(WIFI_STA);
			digitalWrite(LED_AP, HIGH);
		}
		for (int i = 0; i < 30; i++)
		{
			esp_task_wdt_reset();
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
}

void getStaticSimInfo()
{
	Serial.println("[SYSTEM] Khởi động SIM để lấy CCID...");
	resetModem();
	esp_task_wdt_reset();
	if (modem.init())
	{
		esp_task_wdt_reset();
		global_sim_ccid = modem.getSimCCID();
		if (global_sim_ccid == "" || global_sim_ccid == "0")
			global_sim_ccid = "No SIM";
		Serial.println("[SYSTEM] CCID: " + global_sim_ccid);

		modem.sendAT("+CPOWD=1");
		is_modem_sleeping = true;
		Serial.println("[SYSTEM] Đã tắt Module SIM để hạ nhiệt.");
	}
	else
	{
		global_sim_ccid = "Modem Error";
		Serial.println("[SYSTEM] Không thể kết nối Module SIM!");
	}
}

// ================== SETUP ==================
void setup()
{
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

	if (conf_ssid == "")
	{
		Serial.println("[SYSTEM] Chưa có WiFi, tự động vào chế độ Setup...");
		setup_mode = true;
		setup_start_time = millis();
	}
	else
	{
		WiFi.mode(WIFI_STA);
		digitalWrite(LED_AP, HIGH);
		requestWiFiConnect();
	}

	MODBUS_SERIAL.begin(9600, SERIAL_8N1, RXD2, TXD2);
	node.preTransmission(preTransmission);
	node.postTransmission(postTransmission);

	xTaskCreatePinnedToCore(taskWatchdog, "Watchdog", 2048, NULL, 3, NULL, 0);
	xTaskCreatePinnedToCore(taskMQTTPublish, "MQTTPub", 6144, NULL, 2, NULL, 1);
	xTaskCreatePinnedToCore(taskWebServer, "Web", 4096, NULL, 1, NULL, 1);
	xTaskCreatePinnedToCore(taskNetwork, "Network", 8192, NULL, 3, NULL, 0);

	xTaskCreatePinnedToCore(taskButton, "Button", 2048, NULL, 1, NULL, 1);
	xTaskCreatePinnedToCore(taskModbus, "Modbus", 4096, NULL, 2, NULL, 1);
	xTaskCreatePinnedToCore(taskAlarm, "Alarm", 4096, NULL, 3, NULL, 1);
	xTaskCreatePinnedToCore(taskBuzzer, "Buzzer", 2048, NULL, 2, NULL, 1);

	esp_task_wdt_delete(NULL);
	Serial.println("[SYSTEM] Setup hoàn tất.");
}

void loop()
{
	esp_task_wdt_reset();
	vTaskDelay(pdMS_TO_TICKS(1000));
}
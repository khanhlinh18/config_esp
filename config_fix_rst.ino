#define TINY_GSM_MODEM_SIM7600
#define FW_VERSION "1.0.1"   // <-- Tăng lên mỗi lần build firmware mới

// ================== OTA CONFIG ==================
#define OTA_VERSION_URL_HTTP  "http://your-server.com/ota/version.json"
#define OTA_FIRMWARE_URL_HTTP "http://your-server.com/ota/firmware.bin"

#define OTA_VERSION_URL_HTTPS  "https://github.com/khanhlinh18/esp32-OTA/releases/latest/download/version.json"
#define OTA_FIRMWARE_URL_HTTPS "https://github.com/khanhlinh18/esp32-OTA/releases/latest/download/OTA.ino.bin"

#include "ota_ca_cert.h"
#include <esp_task_wdt.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <DNSServer.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <Wire.h>
#include <RTClib.h>

#include "web_config.h"
#include "web_dashboard.h"
#include "web_login.h"
// ================== PINS & HARDWARE ==================
#define SerialAT Serial2
#define MODEM_RX 27
#define MODEM_TX 26
#define MODEM_RST 25

#define MAX485_DE 4
#define MODBUS_SERIAL Serial1
#define RXD2 17
#define TXD2 16
#define SETUP_BUTTON 0
#define LED_AP 13
// ================== SD CARD (SPI) ==================
#define SD_CS   33
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18
// ================== RTC DS1307 (I2C) ==================
#define RTC_SDA 22
#define RTC_SCL 21
// ================== SYSTEM LIMITS ==================
#define MAX_SLAVES 15
// ================== OBJECTS ==================
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
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
const char *MASTER_PIN = "12345"; // PIN reset mật khẩu
String www_pass_str = "cuctac";  

// ================== SESSION TOKEN ==================
#define SESSION_TIMEOUT_MS  (30UL * 60UL * 1000UL)  // 30 phút
String  session_token   = "";
uint32_t session_expiry = 0;

String generateToken() {
    String t = "";
    for (int i = 0; i < 32; i++) {
        t += String((uint8_t)esp_random(), HEX);
    }
    return t;
}

bool isValidSession() {
    if (session_token == "") return false;
    if (!server.hasHeader("Cookie")) return false;
    String cookie = server.header("Cookie");
    if (cookie.indexOf("token=" + session_token) < 0) return false;
    if (millis() > session_expiry) { session_token = ""; return false; }
    session_expiry = millis() + SESSION_TIMEOUT_MS;
    return true;
}

void redirectToLogin() {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
}

void redirectToDashboard() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

volatile bool alarm_cloud = false; // Cờ có cảnh báo mới từ cloud
String alarm_message = "";         // Nội dung cảnh báo từ cloud

const char *TOPIC_CONFIG = "factory/device10/config";
const char *TOPIC_ALARM  = "factory/device10/alarm"; 
const char *TOPIC_INFO   = "factory/device10/info";

// ================== MODBUS STRUCT  ==================
struct SlaveGroup
{
	uint8_t id;
	uint16_t startReg;
	uint8_t count;
	uint8_t dataType;
	float div;
	float lastData[MAX_SLAVES];
	bool isLost;
	bool hasBeenRead; 
};

SlaveGroup myGroups[MAX_SLAVES];
uint8_t totalGroups = 0;

volatile bool setup_mode = false;
uint32_t setup_start_time = 0;

SemaphoreHandle_t configMutex;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t mqttMutex;
SemaphoreHandle_t modbusMutex; // Bảo vệ bus RS485 dùng chung giữa taskModbus và taskAlarm

// ================== SD LOGGER ==================
SemaphoreHandle_t sdMutex;
bool sd_ok      = false;
bool ntp_synced = false;

RTC_DS1307 rtc;
bool rtc_ok     = false;
bool rtc_synced = false;

#define NTP_SERVER1      "pool.ntp.org"
#define NTP_SERVER2      "time.google.com"
#define GMT_OFFSET_SEC   (7 * 3600)   // UTC+7 Việt Nam
#define DST_OFFSET_SEC   0

String getTimestamp()
{
    struct tm t;
    if (getLocalTime(&t))
    {
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        return String(buf);
    }
    if (rtc_ok)
    {
        DateTime now = rtc.now();
        char buf[24];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
        return String(buf);
    }
    return "????-??-?? ??:??:??";
}

String getDateStr()
{
    struct tm t;
    if (getLocalTime(&t))
    {
        char buf[12];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
        return String(buf);
    }
    if (rtc_ok)
    {
        DateTime now = rtc.now();
        char buf[12];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 now.year(), now.month(), now.day());
        return String(buf);
    }
    return "0000-00-00";
}

void sdLog(const char* level, const char* tag, const char* msg)
{
    if (!sd_ok) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    xSemaphoreGive(sdMutex); 

    ensureSDSpace(); 

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    String filename = "/log_" + getDateStr() + ".txt";
    File f = SD.open(filename, FILE_APPEND);
    if (f)
    {
        f.printf("%s [%s] [%s] %s\n", getTimestamp().c_str(), level, tag, msg);
        f.close();
    }
    xSemaphoreGive(sdMutex);
}

void sdLog(const char* level, const char* tag, const String& msg)
{
    sdLog(level, tag, msg.c_str());
}

// ================== LOG MACRO ==================
void _logPrint(const char* tag, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.printf("[%s] %s\n", tag, buf);

    // Ghi SD (phân loại level tự động theo nội dung)
    const char* level = "INFO";
    String lower = String(buf);
    lower.toLowerCase();
    if (lower.indexOf("fail") >= 0 || lower.indexOf("lost") >= 0 ||
        lower.indexOf("error") >= 0 || lower.indexOf("thất bại") >= 0 ||
        lower.indexOf("disconnect") >= 0 || lower.indexOf("timeout") >= 0)
        level = "WARN";

    sdLog(level, tag, buf);
}
#define LOG(tag, fmt, ...) _logPrint(tag, fmt, ##__VA_ARGS__)
#define SD_MIN_FREE_BYTES  (512ULL * 1024ULL)

String deleteOldestLog()
{
    String todayFile = "log_" + getDateStr() + ".txt"; // File hôm nay — không được xóa

    File root = SD.open("/");
    if (!root) return "";

    String oldestName = "";
    File entry = root.openNextFile();
    while (entry)
    {
        String fullName = String(entry.name());
        int slashIdx = fullName.lastIndexOf('/');
        String name = (slashIdx >= 0) ? fullName.substring(slashIdx + 1) : fullName;
        entry.close();

        if (name.startsWith("log_") && name.endsWith(".txt") && name.length() == 18
            && name != todayFile) // Bỏ qua file hôm nay
        {
            if (oldestName == "" || name < oldestName)
                oldestName = name;
        }
        entry = root.openNextFile();
    }
    root.close();

    if (oldestName != "")
    {
        SD.remove("/" + oldestName);
        return oldestName;
    }
    return ""; // Không còn file cũ nào để xóa
}

// Kiểm tra dung lượng SD và xóa log cũ nếu cần
void ensureSDSpace()
{
    if (!sd_ok) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;

    uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();

    while (freeBytes < SD_MIN_FREE_BYTES)
    {
        String deleted = deleteOldestLog();
        if (deleted == "")
        {
            Serial.println("[SD] Cảnh báo: Không còn log cũ để xóa, SD gần đầy!");
            break;
        }

        String auditFile = "/log_" + getDateStr() + ".txt";
        File f = SD.open(auditFile, FILE_APPEND);
        if (f)
        {
            f.printf("%s [WARN] [SD] Xóa log cũ do hết dung lượng: %s (free: %lluKB)\n",
                     getTimestamp().c_str(), deleted.c_str(), freeBytes / 1024);
            f.close();
        }
        Serial.printf("[SD] Xóa log cũ do hết dung lượng: %s (free: %lluKB)\n",
                      deleted.c_str(), freeBytes / 1024);

        freeBytes = SD.totalBytes() - SD.usedBytes();
    }

    xSemaphoreGive(sdMutex);
}

void syncNTP()
{
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    struct tm t;
    int retry = 0;
    while (!getLocalTime(&t) && retry < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(1500));
        retry++;
    }
    if (getLocalTime(&t))
    {
        ntp_synced = true;
        if (rtc_ok) // Luôn cập nhật RTC mỗi lần NTP sync thành công
        {
            rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                                t.tm_hour, t.tm_min, t.tm_sec));
            rtc_synced = true;
            LOG("RTC", "Synced from NTP");
        }
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        LOG("NTP", "Đồng bộ thành công sau %d lần thử: %s", retry + 1, buf);
        sdLog("INFO", "NTP", String("Synced: ") + buf);
    }
    else
    {
        LOG("NTP", "Đồng bộ thất bại, giữ nguyên RTC");
        sdLog("WARN", "NTP", "Sync failed after 20 retries, using RTC time");
    }
}
void preTransmission() { digitalWrite(MAX485_DE, 1); }
void postTransmission() { digitalWrite(MAX485_DE, 0); }

// ================== ALARM FROM CLOUD ==================
#define RELAY_MODBUS_ID    100    
#define RELAY_COIL_ADDR    0x0000  

void writeRelayRS485(bool state)
{
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    node.begin(RELAY_MODBUS_ID, MODBUS_SERIAL);
    preTransmission();
    uint8_t result = node.writeSingleCoil(RELAY_COIL_ADDR, state ? 0xFF00 : 0x0000);
    postTransmission();
    xSemaphoreGive(modbusMutex);
    if (result != node.ku8MBSuccess)
    {
        LOG("ALARM", "Ghi relay RS485 thất bại, mã lỗi: %d", result);
    }
}

void taskAlarm(void *pvParameters)
{
    bool lastRelayState = false; 

    while (1)
    {
        if (alarm_cloud)
        {
            // Kích relay BẬT → chờ 1s → TẮT → chờ 1s → lặp lại khi còn cảnh báo
            writeRelayRS485(true);
            lastRelayState = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            writeRelayRS485(false);
            lastRelayState = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            // Chỉ gửi lệnh tắt 1 lần khi vừa chuyển từ có cảnh báo → không còn
            if (lastRelayState)
            {
                writeRelayRS485(false);
                lastRelayState = false;
            }
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
			myGroups[i].id        = s["id"]       | 1;
			myGroups[i].startReg  = s["start"]     | 0;
			myGroups[i].count     = s["count"]     | 0;
			myGroups[i].dataType  = s["dataType"]  | 1;
			myGroups[i].div       = s["div"]       | 10.0;

			if (myGroups[i].count > MAX_SLAVES) myGroups[i].count = MAX_SLAVES;

			for (int j = 0; j < myGroups[i].count; j++)
				myGroups[i].lastData[j] = -9999.0;
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
	www_pass_str = prefs.getString("www_pass", "cuctac"); 
	prefs.end();

	prefs.begin("modbus_cfg", false);
	String savedSlavesJSON = prefs.getString("slave_cfg", "");
	prefs.end();

	if (savedSlavesJSON.length() > 10)
	{
		LOG("SYSTEM", "Tìm thấy cấu hình Slaves, đang khôi phục...");
		updateConfigFromJSON(savedSlavesJSON.c_str());
	} else {
		LOG("SYSTEM", "Chưa có cấu hình Slaves trong Flash");
	}
}

void setupAPIEndpoints()
{
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);

  server.on("/api/info", HTTP_GET, []() {
    if (!isValidSession()) { server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Unauthorized\"}"); return; }
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
    if (!isValidSession()) { server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Unauthorized\"}"); return; }
    xSemaphoreTake(configMutex, portMAX_DELAY);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    JsonDocument doc;
    
    for (int i = 0; i < totalGroups; i++) {
        JsonObject sObj = doc["slaves"].add<JsonObject>();
        sObj["id"]       = myGroups[i].id;
        sObj["start"]    = myGroups[i].startReg;
        sObj["count"]    = myGroups[i].count;
        sObj["dataType"] = myGroups[i].dataType;
        sObj["div"]      = myGroups[i].div;
    }
    xSemaphoreGive(dataMutex);
    xSemaphoreGive(configMutex);
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response); });

	server.on("/api/save_slaves", HTTP_POST, []()
			  {
    if (!isValidSession()) { server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Unauthorized\"}"); return; }
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        updateConfigFromJSON(body.c_str()); 
        server.send(200, "application/json", "{\"status\":\"OK\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        server.send(400, "text/plain", "Bad Request");
    } });

	// ===== ĐỔI MẬT KHẨU =====
	server.on("/api/change_pass", HTTP_POST, []() {
		if (!isValidSession()) { server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Unauthorized\"}"); return; }
		if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"status\":\"ERR\",\"msg\":\"Bad Request\"}"); return; }
		JsonDocument doc;
		deserializeJson(doc, server.arg("plain"));
		String old_pass = doc["old_pass"] | "";
		String new_pass = doc["new_pass"] | "";
		if (old_pass != www_pass_str) {
			server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Mật khẩu cũ không đúng!\"}");
			return;
		}
		if (new_pass.length() < 4) {
			server.send(400, "application/json", "{\"status\":\"ERR\",\"msg\":\"Mật khẩu mới phải có ít nhất 4 ký tự!\"}");
			return;
		}
		www_pass_str = new_pass;
		prefs.begin("net_cfg", false);
		prefs.putString("www_pass", new_pass);
		prefs.end();
		server.send(200, "application/json", "{\"status\":\"OK\",\"msg\":\"Đổi mật khẩu thành công!\"}");
	});

	// ===== QUÊN MẬT KHẨU (RESET BẰNG PIN BÍ MẬT) =====
	server.on("/api/reset_pass", HTTP_POST, []() {
		if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"status\":\"ERR\",\"msg\":\"Bad Request\"}"); return; }
		JsonDocument doc;
		deserializeJson(doc, server.arg("plain"));
		String pin = doc["pin"] | "";
		String new_pass = doc["new_pass"] | "";
		if (pin != String(MASTER_PIN)) {
			server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Mã PIN không đúng!\"}");
			return;
		}
		if (new_pass.length() < 4) {
			server.send(400, "application/json", "{\"status\":\"ERR\",\"msg\":\"Mật khẩu mới phải có ít nhất 4 ký tự!\"}");
			return;
		}
		www_pass_str = new_pass;
		prefs.begin("net_cfg", false);
		prefs.putString("www_pass", new_pass);
		prefs.end();
		server.send(200, "application/json", "{\"status\":\"OK\",\"msg\":\"Reset mật khẩu thành công!\"}");
	});

	// ===== LOGIN POST =====
	server.on("/api/login", HTTP_POST, []() {
		if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"status\":\"ERR\",\"msg\":\"Bad Request\"}"); return; }
		JsonDocument doc;
		deserializeJson(doc, server.arg("plain"));
		String user = doc["user"] | "";
		String pass = doc["pass"] | "";
		if (user == String(www_user) && pass == www_pass_str) {
			session_token  = generateToken();
			session_expiry = millis() + SESSION_TIMEOUT_MS;
			server.sendHeader("Set-Cookie", "token=" + session_token + "; Path=/; HttpOnly");
			server.send(200, "application/json", "{\"status\":\"OK\"}");
		} else {
			server.send(401, "application/json", "{\"status\":\"ERR\",\"msg\":\"Sai tên đăng nhập hoặc mật khẩu!\"}");
		}
	});

	// ===== LOGOUT =====
	server.on("/api/logout", HTTP_POST, []() {
		session_token = "";
		server.sendHeader("Set-Cookie", "token=; Path=/; Max-Age=0");
		server.send(200, "application/json", "{\"status\":\"OK\"}");
	});

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

	server.on("/save", HTTP_POST, []()
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
    vTaskDelay(pdMS_TO_TICKS(2000));
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
		LOG("AP", "Đã bật AP mode, chờ cấu hình WiFi...");
	}
	dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}
void taskWebServer(void *pvParameters)
{
	setupAPIEndpoints();
	if (setup_mode)
	{
		startAP();
		LOG("WEB", "Đang chạy chế độ Setup (AP)");
	}
	else
	{
		uint32_t wait_start = millis();
		while (WiFi.status() != WL_CONNECTED && !gsm_ok)
		{
			if (millis() - wait_start > 30000) break; // Timeout 30s, mở server dù chưa có mạng
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
		LOG("WEB", "Đang chạy chế độ Runtime. IP: %s",
			WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : global_gsm_ip.c_str());
	}

	server.on("/", HTTP_GET, []() {
		if (setup_mode) {
			server.send(200, "text/html", htmlForm);
		} else {
			if (!isValidSession()) return redirectToLogin();
			server.send(200, "text/html", htmlDashboard);
		}
	});

	server.on("/login", HTTP_GET, []() {
		if (setup_mode) { redirectToDashboard(); return; }
		if (isValidSession()) { redirectToDashboard(); return; }
		server.send(200, "text/html", htmlLogin);
	});

	server.onNotFound([]() {
		if (setup_mode) {
			server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
			server.send(302, "text/plain", "");
		} else {
			redirectToLogin();
		}
	});

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
	bool resetTriggered = false; 

	while (1)
	{
		if (digitalRead(SETUP_BUTTON) == LOW)
		{
			if (!pressing)
			{
				pressTime = millis();
				pressing = true;
				resetTriggered = false;
			}
			// Nhấn giữ hơn 5 giây — chỉ xử lý 1 lần (resetTriggered)
			if (pressing && !resetTriggered && (millis() - pressTime > 5000))
			{
				resetTriggered = true;
				Serial.println("[SYSTEM] Nút giữ 5 giây: xóa cấu hình và reboot");

				prefs.begin("net_cfg", false);
				prefs.clear();
				prefs.end();

				esp_task_wdt_reset();
				vTaskDelay(pdMS_TO_TICKS(300));
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
		if (length > 4094)
			return;
		static char jsonStr[4096];
		memcpy(jsonStr, payload, length);
		jsonStr[length] = '\0';
		LOG("MQTT", "Nhận cấu hình mới từ cloud");
		updateConfigFromJSON(jsonStr);
	}
	else if (strcmp(topic, TOPIC_ALARM) == 0)
	{
		if (length > 0)
		{
			// Chuyển payload thành String
			char buf[256];
			int len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
			memcpy(buf, payload, len);
			buf[len] = '\0';
			String msg = String(buf);

			if (msg == "normal")
			{
				// Không có cảnh báo → tắt relay nếu đang bật
				if (alarm_cloud)
				{
					alarm_cloud = false;
					alarm_message = "";
					LOG("ALARM", "Cloud: Hết cảnh báo → tắt relay");
					sdLog("INFO", "ALARM", "Cloud cleared alarm");
				}
			}
			else
			{
				// Có cảnh báo mới
				alarm_cloud = true;
				alarm_message = msg;
				LOG("ALARM", "Có cảnh báo: %s", msg.c_str());
				sdLog("WARN", "ALARM", String("Có cảnh báo: ") + msg);
			}
		}
	}
}

void requestWiFiConnect()
{
	static unsigned long last_attempt = 0;
	if (millis() - last_attempt < 20000)
		return;
	last_attempt = millis();

	LOG("NET", "Đang thử kết nối WiFi...");
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
	LOG("NET", "=== GSM START ===");
	resetModem();
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

	LOG("NET", "IMSI: %s -> APN: %s", imsi.c_str(), auto_apn.c_str());

	if (!modem.gprsConnect(auto_apn.c_str(), "", ""))
		return false;
	if (!modem.isGprsConnected())
		return false;
	LOG("NET", "GSM Connected!");
	return true;
}

void connectMQTT() {
  LOG("MQTT", "Đang thử kết nối %s:%d...", conf_mqtt_server.c_str(), conf_mqtt_port);
  mqtt.setServer(conf_mqtt_server.c_str(), conf_mqtt_port);
  mqtt.setCallback(mqttCallback);

  String clientId = "ESP32_" + WiFi.macAddress();
  clientId.replace(":", "");

  if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
    bool success = mqtt.connect(clientId.c_str());
    
    if (success) {
      LOG("MQTT", "Connected!");
      mqtt.subscribe(TOPIC_CONFIG);
      mqtt.subscribe(TOPIC_ALARM);
    } else {
      LOG("MQTT", "Kết nối thất bại, mã lỗi: %d", mqtt.state());
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
		static bool prev_wifi_ok = false;

		if (current_wifi_ok)
		{
			if (!prev_wifi_ok) 
			{
				LOG("NET", "WiFi connected: %s IP:%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
				wifi_start_time = millis();
			}
			prev_wifi_ok = true;
			network_ok = true; 
			if (!is_modem_sleeping)
			{
				LOG("NET", "Đã có WiFi, tắt SIM tiết kiệm điện");
				modem.sendAT("+CPOWD=1");
				is_modem_sleeping = true;
				gsm_ok = false;
			}
			// Sync NTP: lần đầu hoặc mỗi 6 giờ để cập nhật RTC
			static uint32_t last_ntp_sync = 0;
			if (!ntp_synced || millis() - last_ntp_sync > 6UL * 3600UL * 1000UL)
			{
				syncNTP();
				last_ntp_sync = millis();
			}
			mqtt.setClient(wifiClient);
			last_wifi_recheck = millis();
		}
		else
		{
			if (prev_wifi_ok) // Vừa mất WiFi
			{
				LOG("NET", "WiFi disconnected!");
				wifi_start_time = millis();
				network_ok = false; 
			}
			prev_wifi_ok = false;
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
						network_ok = true; 
						mqtt.setClient(gsmClient);
						last_wifi_recheck = millis();
						last_mqtt_retry = 0;

						vTaskDelay(pdMS_TO_TICKS(2000));
						global_gsm_ip = modem.localIP().toString();
						if (global_gsm_ip == "0.0.0.0" || global_gsm_ip == "") {
							vTaskDelay(pdMS_TO_TICKS(3000));
							global_gsm_ip = modem.localIP().toString();
						}
						LOG("NET", "GSM connected, IP: %s", global_gsm_ip.c_str());

						// Sync NTP qua GSM — retry tối đa 3 lần, mỗi lần cách 5s
						if (!ntp_synced) {
							for (int _r = 0; _r < 3 && !ntp_synced; _r++) {
								vTaskDelay(pdMS_TO_TICKS(5000));
								syncNTP();
							}
						}
					}
					else
					{
						wifi_start_time = millis();
						LOG("NET", "GSM connect failed, retry sau 15s");
					}
				}
			}
			else
			{
				if (millis() - last_wifi_recheck > WIFI_RECHECK_INTERVAL)
				{
					LOG("NET", "Tạm dừng GSM, kiểm tra lại WiFi sau 10 phút");
					if (mqtt.connected())
					{
						LOG("NET", "Tạm dừng MQTT để quét WiFi");
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
						LOG("NET", "Không tìm thấy WiFi, quay lại GSM");
						WiFi.mode(WIFI_OFF);
						gsmClient.stop();
						if (!modem.isGprsConnected())
						{
							modem.gprsConnect(auto_apn.c_str(), "", "");
						}
						last_mqtt_retry = 0;
					}
					last_wifi_recheck = millis();
				}
			}
		}
		// --- QUẢN LÝ KẾT NỐI MQTT ---
		if (current_wifi_ok || (gsm_ok && !is_modem_sleeping)){
      if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool prev_mqtt_ok = mqtt_ok;
        mqtt_ok = mqtt.connected();
        xSemaphoreGive(mqttMutex);
        // Log thay đổi trạng thái MQTT
        if (mqtt_ok && !prev_mqtt_ok)
            LOG("MQTT", "Connected to broker");
        else if (!mqtt_ok && prev_mqtt_ok)
            LOG("MQTT", "Disconnected from broker");
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
			xSemaphoreGive(configMutex);

			if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
				vTaskDelay(pdMS_TO_TICKS(150));
				continue;
			}
				node.begin(id, MODBUS_SERIAL);
				uint8_t result = node.readHoldingRegisters(start, qty);
			xSemaphoreGive(modbusMutex);

				xSemaphoreTake(dataMutex, portMAX_DELAY);
				if (result == node.ku8MBSuccess)
				{
					if (myGroups[i].isLost && myGroups[i].hasBeenRead) {
						char _buf[40]; snprintf(_buf, sizeof(_buf), "Slave ID%d recovered", myGroups[i].id);
						sdLog("INFO", "MODBUS", _buf);
					}
					myGroups[i].isLost = false;
					myGroups[i].hasBeenRead = true;

					int bufferOffset = 0;
					for (int j = 0; j < myGroups[i].count && j < MAX_SLAVES; j++)
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
					if (!myGroups[i].isLost && myGroups[i].hasBeenRead) {
						char _buf[40]; snprintf(_buf, sizeof(_buf), "Slave ID%d LOST", myGroups[i].id);
						sdLog("WARN", "MODBUS", _buf);
					}
					myGroups[i].isLost = true;

					// FIX #2: Giới hạn j < MAX_SLAVES để an toàn
					for (int j = 0; j < myGroups[i].count && j < MAX_SLAVES; j++)
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

      // 1. LẤY THÔNG TIN MẠNG 
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

        snprintf(slaveTopic, sizeof(slaveTopic), "factory/device10/slave%d", myGroups[i].id);

        int offset = 0;
        if (myGroups[i].isLost) {
            snprintf(payload, sizeof(payload), "{\"id%d\":{\"val\":\"ERR\"}}", myGroups[i].id);
        } else {
            offset = snprintf(payload, sizeof(payload), "{\"id%d\":[", myGroups[i].id);
            for (int j = 0; j < myGroups[i].count && j < MAX_SLAVES; j++) {
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
// ================== TASK OTA ==================
// So sánh version string kiểu "1.0.0" < "1.0.1" → true
bool isNewerVersion(const String& current, const String& latest) {
    int c1=0,c2=0,c3=0, l1=0,l2=0,l3=0;
    sscanf(current.c_str(), "%d.%d.%d", &c1, &c2, &c3);
    sscanf(latest.c_str(),  "%d.%d.%d", &l1, &l2, &l3);
    if (l1 != c1) return l1 > c1;
    if (l2 != c2) return l2 > c2;
    return l3 > c3;
}

// ---- BẢN HTTP (không cần CA cert) ----
void taskOTA_HTTP(void *pvParameters)
{
    // Chờ hệ thống kết nối mạng ổn định sau khi boot
    vTaskDelay(pdMS_TO_TICKS(30000));

    const uint32_t CHECK_INTERVAL = 24UL * 60UL * 60UL * 1000UL; // 24 giờ
    uint32_t last_check = 0;

    while (1)
    {
        // Kiểm tra mỗi 24 giờ, hoặc ngay lần đầu
        if (millis() - last_check < CHECK_INTERVAL && last_check != 0) {
            vTaskDelay(pdMS_TO_TICKS(60000)); 
            continue;
        }

        // Chỉ OTA khi đang có mạng
        if (!network_ok) {
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        last_check = millis();
        LOG("OTA", "Kiểm tra firmware mới...");

        // Bước 1: Lấy version.json từ server
        HTTPClient http;
        http.begin(OTA_VERSION_URL_HTTP);
        int httpCode = http.GET();

        if (httpCode != 200) {
            LOG("OTA", "Không lấy được version, HTTP code: %d", httpCode);
            http.end();
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        String payload = http.getString();
        http.end();

        // Parse JSON lấy version
        JsonDocument doc;
        if (deserializeJson(doc, payload) != DeserializationError::Ok) {
            LOG("OTA", "JSON version không hợp lệ: %s", payload.c_str());
            continue;
        }
        String latestVersion = doc["version"] | "";
        if (latestVersion == "") {
            LOG("OTA", "Không tìm thấy field 'version' trong JSON");
            continue;
        }

        LOG("OTA", "Version hiện tại: %s | Mới nhất: %s", FW_VERSION, latestVersion.c_str());

        // Bước 2: So sánh version
        if (!isNewerVersion(FW_VERSION, latestVersion)) {
            LOG("OTA", "Đang dùng firmware mới nhất, bỏ qua");
            continue;
        }

        // Bước 3: Có bản mới → tiến hành OTA
        LOG("OTA", "Có bản mới %s → bắt đầu tải firmware...", latestVersion.c_str());
        sdLog("INFO", "OTA", String("Updating to v") + latestVersion);

        WiFiClient otaClient;
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Cần thiết cho GitHub redirect

        // Callback khi update xong → tự reboot
        httpUpdate.onEnd([](){ LOG("OTA", "Update xong, reboot..."); });
        httpUpdate.onError([](int err){ LOG("OTA", "Lỗi OTA: %d", err); });
        httpUpdate.onProgress([](int cur, int total){
            static int lastPct = -1;
            int pct = (cur * 100) / total;
            if (pct != lastPct && pct % 10 == 0) { // Log mỗi 10%
                LOG("OTA", "Tiến trình: %d%%", pct);
                lastPct = pct;
            }
        });

        t_httpUpdate_return ret = httpUpdate.update(otaClient, OTA_FIRMWARE_URL_HTTP);

        switch (ret) {
            case HTTP_UPDATE_OK:
                break;
            case HTTP_UPDATE_FAILED:
                LOG("OTA", "Update thất bại: %s", httpUpdate.getLastErrorString().c_str());
                sdLog("WARN", "OTA", String("Failed: ") + httpUpdate.getLastErrorString());
                break;
            case HTTP_UPDATE_NO_UPDATES:
                LOG("OTA", "Server báo không có bản mới");
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

// ---- BẢN HTTPS (có CA cert) ----
void taskOTA_HTTPS(void *pvParameters)
{
    // Chờ hệ thống kết nối mạng ổn định sau khi boot
    LOG("OTA", "Task OTA khởi động, chờ 30s...");
    vTaskDelay(pdMS_TO_TICKS(30000));
    LOG("OTA", "Bắt đầu vòng lặp OTA, network_ok=%d", (int)network_ok);

    const uint32_t CHECK_INTERVAL = 24UL * 60UL * 60UL * 1000UL; // 24 giờ
    uint32_t last_check = 0;

    while (1)
    {
        if (millis() - last_check < CHECK_INTERVAL && last_check != 0) {
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        if (!network_ok) {
            LOG("OTA", "Chờ mạng... network_ok=%d", (int)network_ok);
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        last_check = millis();
        LOG("OTA", "Kiểm tra firmware mới (HTTPS)...");

        WiFiClientSecure versionClient;
        versionClient.setInsecure();
        // versionClient.setCACert(OTA_CA_CERT);

        HTTPClient https;
        https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        https.begin(versionClient, OTA_VERSION_URL_HTTPS);
        int httpCode = https.GET();

        if (httpCode != 200) {
            LOG("OTA", "Không lấy được version, HTTP code: %d", httpCode);
            https.end();
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        String payload = https.getString();
        https.end();

        // Parse JSON
        JsonDocument doc;
        if (deserializeJson(doc, payload) != DeserializationError::Ok) {
            LOG("OTA", "JSON version không hợp lệ: %s", payload.c_str());
            continue;
        }
        String latestVersion = doc["version"] | "";
        if (latestVersion == "") {
            LOG("OTA", "Không tìm thấy field 'version' trong JSON");
            continue;
        }

        LOG("OTA", "Version hiện tại: %s | Mới nhất: %s", FW_VERSION, latestVersion.c_str());

        // So sánh version
        if (!isNewerVersion(FW_VERSION, latestVersion)) {
            LOG("OTA", "Đang dùng firmware mới nhất, bỏ qua");
            continue;
        }

        // Có bản mới → OTA qua HTTPS
        LOG("OTA", "Có bản mới %s → bắt đầu tải firmware (HTTPS)...", latestVersion.c_str());
        sdLog("INFO", "OTA", String("Updating to v") + latestVersion);

        WiFiClientSecure otaClient;
        otaClient.setInsecure();
        // otaClient.setCACert(OTA_CA_CERT); 
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        httpUpdate.onProgress([](int cur, int total){
            static int lastPct = -1;
            int pct = (cur * 100) / total;
            if (pct != lastPct && pct % 10 == 0) {
                LOG("OTA", "Tiến trình: %d%%", pct);
                lastPct = pct;
            }
        });

        t_httpUpdate_return ret = httpUpdate.update(otaClient, OTA_FIRMWARE_URL_HTTPS);

        switch (ret) {
            case HTTP_UPDATE_OK:
                break;
            case HTTP_UPDATE_FAILED:
                LOG("OTA", "Update thất bại: %s", httpUpdate.getLastErrorString().c_str());
                sdLog("WARN", "OTA", String("Failed: ") + httpUpdate.getLastErrorString());
                break;
            case HTTP_UPDATE_NO_UPDATES:
                LOG("OTA", "Server báo không có bản mới");
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
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
			LOG("WATCHDOG", "Mất kết nối quá 1 giờ, reboot...");
			vTaskDelay(pdMS_TO_TICKS(500));
			ESP.restart();
		}

		if (millis() - system_start_time > MAINTENANCE_REBOOT)
		{
			LOG("WATCHDOG", "Reboot định kỳ 7 ngày");
			vTaskDelay(pdMS_TO_TICKS(500));
			ESP.restart();
		}
		if (setup_mode && (millis() - setup_start_time > AP_TIMEOUT_MS))
		{
			LOG("WATCHDOG", "Hết thời gian Setup 3 phút, tắt AP chuyển 4G");

			setup_mode = false;
			WiFi.softAPdisconnect(true);
			WiFi.mode(WIFI_STA);
			digitalWrite(LED_AP, HIGH);
			LOG("AP", "AP tắt, chuyển sang chế độ STA");
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
	LOG("SYSTEM", "Khởi động SIM để lấy CCID...");
	resetModem();
	esp_task_wdt_reset();
	if (modem.init())
	{
		esp_task_wdt_reset();
		global_sim_ccid = modem.getSimCCID();
		if (global_sim_ccid == "" || global_sim_ccid == "0")
			global_sim_ccid = "No SIM";
		LOG("SYSTEM", "CCID: %s", global_sim_ccid.c_str());

		modem.sendAT("+CPOWD=1");
		is_modem_sleeping = true;
		LOG("SYSTEM", "Đã tắt Module SIM để hạ nhiệt");
	}
	else
	{
		global_sim_ccid = "Modem Error";
		LOG("SYSTEM", "Không thể kết nối Module SIM!");
	}
}

// ================== SETUP ==================
void setup()
{
	Serial.begin(115200);
	SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
	esp_task_wdt_init(60, true);

	configMutex = xSemaphoreCreateMutex();
	dataMutex   = xSemaphoreCreateMutex();
	mqttMutex   = xSemaphoreCreateMutex();
	modbusMutex = xSemaphoreCreateMutex();
	sdMutex     = xSemaphoreCreateMutex();

	// ===== KHỞI TẠO RTC DS1307 =====
	Wire.begin(RTC_SDA, RTC_SCL);
	if (rtc.begin())
	{
		rtc_ok = true;
		if (!rtc.isrunning())
		{
			// RTC bị mất nguồn pin, đặt thời gian tạm từ lúc compile
			rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
			LOG("RTC", "RTC mất pin, đặt thời gian compile tạm thời");
		}
		else
		{
			DateTime now = rtc.now();
			LOG("RTC", "Thời gian: %04d-%02d-%02d %02d:%02d:%02d",
				now.year(), now.month(), now.day(),
				now.hour(), now.minute(), now.second());
		}
	}
	else
	{
		rtc_ok = false;
		LOG("RTC", "Không tìm thấy DS1307!");
	}

	// ===== KHỞI TẠO SD CARD =====
	SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
	if (SD.begin(SD_CS))
	{
		sd_ok = true;
		LOG("SD", "SD card sẵn sàng");
		String bootMsg = String("=== ESP32 RESTARTED === RTC:") + (rtc_ok ? "OK" : "FAIL")
		               + " SD:OK Time:" + getTimestamp();
		sdLog("INFO", "BOOT", bootMsg);
	}
	else
	{
		sd_ok = false;
		LOG("SD", "Không tìm thấy SD card!");
	}

	pinMode(MAX485_DE, OUTPUT);
	pinMode(MODEM_RST, OUTPUT);

	mqtt.setBufferSize(4096);
	mqtt.setKeepAlive(120);
	mqtt.setSocketTimeout(30);

	getStaticSimInfo();
	loadConfig();

	if (conf_ssid == "")
	{
		LOG("SYSTEM", "Chưa có WiFi, tự động vào chế độ Setup...");
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

	xTaskCreatePinnedToCore(taskWatchdog,    "Watchdog", 4096, NULL, 3, NULL, 0);
	xTaskCreatePinnedToCore(taskMQTTPublish, "MQTTPub",  6144, NULL, 2, NULL, 1);
	xTaskCreatePinnedToCore(taskWebServer,   "Web",      8192, NULL, 1, NULL, 1);
	xTaskCreatePinnedToCore(taskNetwork,     "Network",  8192, NULL, 3, NULL, 0);

	xTaskCreatePinnedToCore(taskButton,  "Button", 4096, NULL, 1, NULL, 1);
	xTaskCreatePinnedToCore(taskModbus,  "Modbus", 4096, NULL, 2, NULL, 1);
	xTaskCreatePinnedToCore(taskAlarm,   "Alarm",  2048, NULL, 2, NULL, 1);

	// xTaskCreatePinnedToCore(taskOTA_HTTP,  "OTA", 8192, NULL, 1, NULL, 0);
	xTaskCreatePinnedToCore(taskOTA_HTTPS, "OTA", 12288, NULL, 1, NULL, 0);

	esp_task_wdt_delete(NULL);
	LOG("SYSTEM", "Setup hoàn tất");
}

void loop()
{
	esp_task_wdt_reset();
	vTaskDelay(pdMS_TO_TICKS(1000));
}

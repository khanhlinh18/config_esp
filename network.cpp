#include "network.h"
#include <esp_task_wdt.h>

// ================== NTP SYNC ==================
void syncNTP() {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    struct tm t;
    int retry = 0;
    while (!getLocalTime(&t) && retry < 20) { vTaskDelay(pdMS_TO_TICKS(1500)); retry++; }
    if (getLocalTime(&t)) {
        ntp_synced = true;
        if (rtc_ok) {
            rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                                t.tm_hour, t.tm_min, t.tm_sec));
            rtc_synced = true;
            LOG("RTC", "Synced from NTP");
        }
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        LOG("NTP", "Đồng bộ thành công sau %d lần thử: %s", retry + 1, buf);
        sdLog("INFO", "NTP", String("Synced: ") + buf);
    } else {
        LOG("NTP", "Đồng bộ thất bại, giữ nguyên RTC");
        sdLog("WARN", "NTP", "Sync failed after 20 retries");
    }
}

// ================== WIFI ==================
void requestWiFiConnect() {
    static unsigned long last_attempt = 0;
    if (millis() - last_attempt < 20000) return;
    last_attempt = millis();
    LOG("NET", "Đang thử kết nối WiFi...");
    WiFi.begin(conf_ssid.c_str(), conf_pass.c_str());
}

// ================== INTERNET CHECK ==================
bool isInternetReachable() {
    Serial.println("[NET] Đang kiểm tra internet (8.8.8.8:53)...");
    WiFiClient testClient;
    testClient.setTimeout(3000);
    bool ok = testClient.connect("8.8.8.8", 53);
    testClient.stop();
    Serial.printf("[NET] Internet check: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

// ================== MODEM ==================
void resetModem() {
    pinMode(MODEM_RST, OUTPUT);
    digitalWrite(MODEM_RST, HIGH); vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(MODEM_RST, LOW);  vTaskDelay(pdMS_TO_TICKS(1000));
    digitalWrite(MODEM_RST, HIGH);
    for (int i = 0; i < 10; i++) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(1000)); }
}

// ================== GSM ==================
bool connectGSM() {
    LOG("NET", "=== GSM START ===");
    resetModem();
    esp_task_wdt_reset();
    if (!modem.init()) return false;
    esp_task_wdt_reset();
    uint32_t start = millis();
    while (!modem.isNetworkConnected()) {
        esp_task_wdt_reset();
        if (millis() - start > 60000) return false;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    String imsi = modem.getIMSI();
    if      (imsi.startsWith("45204")) auto_apn = "v-internet";
    else if (imsi.startsWith("45201")) auto_apn = "m-wap";
    else if (imsi.startsWith("45202")) auto_apn = "m3-world";
    else if (imsi.startsWith("45205")) auto_apn = "vietnamobile";
    else if (imsi.startsWith("45208")) auto_apn = "m9-itelecom";
    LOG("NET", "IMSI: %s -> APN: %s", imsi.c_str(), auto_apn.c_str());
    if (!modem.gprsConnect(auto_apn.c_str(), "", "")) return false;
    if (!modem.isGprsConnected()) return false;
    LOG("NET", "GSM Connected!");
    return true;
}

// ================== MQTT ==================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, TOPIC_CONFIG) == 0) {
        if (length > 4094) return;
        static char jsonStr[4096];
        memcpy(jsonStr, payload, length);
        jsonStr[length] = '\0';
        LOG("MQTT", "Nhận cấu hình mới từ cloud");
        updateConfigFromJSON(jsonStr);
    }
    else if (strcmp(topic, TOPIC_OTA) == 0) {
        if (length > 0) {
            char buf[32];
            int len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
            memcpy(buf, payload, len);
            buf[len] = '\0';
            String msg = String(buf); msg.trim(); msg.replace("\"", "");
            if (msg == "update") {
                LOG("OTA", "Nhận lệnh OTA từ cloud");
                sdLog("INFO", "OTA", "OTA triggered by cloud");
                ota_requested = true;
            }
        }
    }
    else if (strcmp(topic, TOPIC_ALARM) == 0) {
        if (length > 0) {
            char buf[256];
            int len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
            memcpy(buf, payload, len);
            buf[len] = '\0';
            String msg = String(buf);
            if (msg == "normal") {
                if (alarm_cloud) {
                    alarm_cloud = false; alarm_message = "";
                    LOG("ALARM", "Hết cảnh báo → tắt relay");
                    sdLog("INFO", "ALARM", "Cloud cleared alarm");
                }
            } else {
                alarm_cloud = true; alarm_message = msg;
                LOG("ALARM", "Có cảnh báo: %s", msg.c_str());
                sdLog("WARN", "ALARM", String("Có cảnh báo: ") + msg);
            }
        }
    }
}

void connectMQTT() {
    LOG("MQTT", "Đang thử kết nối %s:%d...", conf_mqtt_server.c_str(), conf_mqtt_port);
    mqtt.setServer(conf_mqtt_server.c_str(), conf_mqtt_port);
    mqtt.setCallback(mqttCallback);
    String clientId = "ESP32_" + WiFi.macAddress(); clientId.replace(":", "");
    if (xSemaphoreTake(mqttMutex, portMAX_DELAY) == pdTRUE) {
        bool success = mqtt.connect(clientId.c_str());
        if (success) {
            LOG("MQTT", "Connected!");
            mqtt.subscribe(TOPIC_CONFIG);
            mqtt.subscribe(TOPIC_ALARM);
            mqtt.subscribe(TOPIC_OTA);
        } else {
            LOG("MQTT", "Kết nối thất bại, mã lỗi: %d", mqtt.state());
        }
        xSemaphoreGive(mqttMutex);
    }
}

// ================== NETWORK TASK ==================
void taskNetwork(void* pvParameters) {
    unsigned long wifi_start_time    = millis();
    unsigned long last_wifi_recheck  = millis();
    unsigned long last_mqtt_retry    = 0;
    const unsigned long WIFI_WAIT_TIME        = 15000;
    const unsigned long WIFI_RECHECK_INTERVAL = 600000;

    while (1) {
        if (setup_mode) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

        bool current_wifi_ok = (WiFi.status() == WL_CONNECTED);
        static bool prev_wifi_ok = false;

        if (current_wifi_ok) {
            if (!prev_wifi_ok) {
                LOG("NET", "WiFi connected: %s IP:%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
                wifi_start_time = millis();
                // Kiểm tra internet ngay khi vừa connect WiFi
                if (!isInternetReachable()) {
                    LOG("NET", "WiFi có IP nhưng KHÔNG có internet → chuyển 4G");
                    sdLog("WARN", "NET", "WiFi no internet, switching to GSM");
                    prev_wifi_ok = false;
                    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gsm_ok = connectGSM();
                    if (gsm_ok) {
                        is_modem_sleeping = false;
                        network_ok = true;
                        mqtt.setClient(gsmClient);
                        last_mqtt_retry = 0;
                        global_gsm_ip = modem.localIP().toString();
                        LOG("NET", "GSM connected (fallback), IP: %s", global_gsm_ip.c_str());
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
            }
            prev_wifi_ok = true;
            network_ok   = true;

            // Định kỳ kiểm tra internet vẫn còn thông
            static uint32_t last_inet_check = 0;
            if (millis() - last_inet_check > 30000) {
                last_inet_check = millis();
                if (!isInternetReachable()) {
                    LOG("NET", "WiFi mất internet → chuyển ngay sang 4G");
                    sdLog("WARN", "NET", "WiFi lost internet, switching to GSM");
                    prev_wifi_ok = false;
                    network_ok   = false;
                    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gsm_ok = connectGSM();
                    if (gsm_ok) {
                        is_modem_sleeping = false;
                        network_ok = true;
                        mqtt.setClient(gsmClient);
                        last_mqtt_retry = 0;
                        global_gsm_ip = modem.localIP().toString();
                        LOG("NET", "GSM connected (fallback), IP: %s", global_gsm_ip.c_str());
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
            }

            if (!is_modem_sleeping) {
                LOG("NET", "Đã có WiFi, tắt SIM tiết kiệm điện");
                modem.sendAT("+CPOWD=1");
                is_modem_sleeping = true;
                gsm_ok = false;
            }
            static uint32_t last_ntp_sync = 0;
            if (!ntp_synced || millis() - last_ntp_sync > 6UL * 3600UL * 1000UL) {
                syncNTP(); last_ntp_sync = millis();
            }
            mqtt.setClient(wifiClient);
            last_wifi_recheck = millis();
        } else {
            if (prev_wifi_ok) {
                LOG("NET", "WiFi disconnected!");
                wifi_start_time = millis();
                network_ok = false;
            }
            prev_wifi_ok = false;
            if (!gsm_ok || is_modem_sleeping) {
                if (millis() - wifi_start_time < WIFI_WAIT_TIME) {
                    requestWiFiConnect();
                } else {
                    WiFi.disconnect(); WiFi.mode(WIFI_OFF);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gsm_ok = connectGSM();
                    if (gsm_ok) {
                        is_modem_sleeping = false;
                        network_ok = true;
                        mqtt.setClient(gsmClient);
                        last_wifi_recheck = millis();
                        last_mqtt_retry   = 0;
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        global_gsm_ip = modem.localIP().toString();
                        if (global_gsm_ip == "0.0.0.0" || global_gsm_ip == "") {
                            vTaskDelay(pdMS_TO_TICKS(3000));
                            global_gsm_ip = modem.localIP().toString();
                        }
                        LOG("NET", "GSM connected, IP: %s", global_gsm_ip.c_str());
                        if (!ntp_synced) {
                            for (int _r = 0; _r < 3 && !ntp_synced; _r++) {
                                vTaskDelay(pdMS_TO_TICKS(5000)); syncNTP();
                            }
                        }
                    } else {
                        wifi_start_time = millis();
                        LOG("NET", "GSM connect failed, retry sau 15s");
                    }
                }
            } else {
                if (millis() - last_wifi_recheck > WIFI_RECHECK_INTERVAL) {
                    LOG("NET", "Kiểm tra lại WiFi...");
                    if (mqtt.connected()) { LOG("NET", "Tạm dừng MQTT để quét WiFi"); mqtt.disconnect(); }
                    last_wifi_recheck = millis();
                    WiFi.mode(WIFI_STA); requestWiFiConnect();
                    unsigned long check_start = millis();
                    bool found_wifi = false;
                    while (millis() - check_start < 15000) {
                        if (WiFi.status() == WL_CONNECTED) { found_wifi = true; break; }
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }
                    if (!found_wifi) {
                        LOG("NET", "Không tìm thấy WiFi, quay lại GSM");
                        WiFi.mode(WIFI_OFF); gsmClient.stop();
                        if (!modem.isGprsConnected()) modem.gprsConnect(auto_apn.c_str(), "", "");
                        last_mqtt_retry = 0;
                    }
                    last_wifi_recheck = millis();
                }
            }
        }

        if (current_wifi_ok || (gsm_ok && !is_modem_sleeping)) {
            if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                bool prev_mqtt_ok = mqtt_ok;
                mqtt_ok = mqtt.connected();
                xSemaphoreGive(mqttMutex);
                if (mqtt_ok && !prev_mqtt_ok)  LOG("MQTT", "Connected to broker");
                else if (!mqtt_ok && prev_mqtt_ok) LOG("MQTT", "Disconnected from broker");
            }
            if (!mqtt_ok) {
                if (millis() - last_mqtt_retry > 10000 || last_mqtt_retry == 0) {
                    connectMQTT(); last_mqtt_retry = millis();
                }
            } else {
                if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    mqtt.loop(); xSemaphoreGive(mqttMutex);
                }
            }
        } else { mqtt_ok = false; }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

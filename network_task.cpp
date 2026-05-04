#include "network_task.h"
#include "mqtt_task.h"

void requestWiFiConnect() {
    static unsigned long last_attempt = 0;
    if (millis() - last_attempt < 20000) return; 
    last_attempt = millis();

    Serial.println("[NET] Đang thử kết nối WiFi...");
    WiFi.begin(conf_ssid.c_str(), conf_pass.c_str());
}

void resetModem() {
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH); vTaskDelay(pdMS_TO_TICKS(100));
  digitalWrite(MODEM_RST, LOW);  vTaskDelay(pdMS_TO_TICKS(1000));
  digitalWrite(MODEM_RST, HIGH); 
  for(int i = 0; i < 10; i++){
    esp_task_wdt_reset(); 
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool connectGSM() {
  Serial.println("=== GSM START ===");
  resetModem();
  modem.restart();

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

  if (imsi.startsWith("45204")) auto_apn = "v-internet";      
  else if (imsi.startsWith("45201")) auto_apn = "m-wap";      
  else if (imsi.startsWith("45202")) auto_apn = "m3-world";    
  else if (imsi.startsWith("45205")) auto_apn = "vietnamobile";
  else if (imsi.startsWith("45208")) auto_apn = "m9-itelecom";
  
  Serial.printf("[GSM] IMSI: %s -> Auto APN: %s\n", imsi.c_str(), auto_apn.c_str());

  if (!modem.gprsConnect(auto_apn.c_str(), "", "")) return false; 
  if (!modem.isGprsConnected()) return false;
  
  Serial.println("[NET] GSM Connected!");
  return true; 
}

void getStaticSimInfo() {
  Serial.println("[SYSTEM] Khởi động SIM để lấy CCID...");
  resetModem(); 
  esp_task_wdt_reset();
  if (modem.init()) {
    esp_task_wdt_reset();
    global_sim_ccid = modem.getSimCCID();
    if (global_sim_ccid == "" || global_sim_ccid == "0") global_sim_ccid = "No SIM";
    Serial.println("[SYSTEM] CCID: " + global_sim_ccid);
    
    modem.sendAT("+CPOWD=1"); 
    is_modem_sleeping = true;
    Serial.println("[SYSTEM] Đã tắt Module SIM để hạ nhiệt.");
  } else {
    global_sim_ccid = "Modem Error";
    Serial.println("[SYSTEM] Không thể kết nối Module SIM!");
  }
}

// ================== NETWORK TASKS ==================
void taskNetwork(void *pvParameters) {
    unsigned long wifi_start_time = millis();
    unsigned long last_wifi_recheck = millis(); 
    unsigned long last_mqtt_retry = 0;
    
    const unsigned long WIFI_WAIT_TIME = 15000;         
    const unsigned long WIFI_RECHECK_INTERVAL = 600000; 

    while (1) {
        bool current_wifi_ok = (WiFi.status() == WL_CONNECTED);

        if (current_wifi_ok) {
            if (!is_modem_sleeping) {
                Serial.println("[NET] Đã có WiFi, tắt SIM...");
                modem.sendAT("+CPOWD=1"); 
                is_modem_sleeping = true;
                gsm_ok = false;
            }
            mqtt.setClient(wifiClient);
            last_wifi_recheck = millis(); 
        } 
        else {
            if (!gsm_ok || is_modem_sleeping) {
                if (millis() - wifi_start_time < WIFI_WAIT_TIME) {
                    requestWiFiConnect(); 
                } else {
                    WiFi.disconnect();
                    WiFi.mode(WIFI_OFF); 
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gsm_ok = connectGSM(); 
                    if (gsm_ok) {
                        is_modem_sleeping = false;
                        mqtt.setClient(gsmClient);
                        last_wifi_recheck = millis();
                        last_mqtt_retry = 0; // Thử MQTT ngay khi GSM vừa lên
                    } else {
                        wifi_start_time = millis(); 
                    }
                }
            } 
            else {
                if (millis() - last_wifi_recheck > WIFI_RECHECK_INTERVAL) {
                    Serial.println("[NET] Tạm dừng GSM để kiểm tra WiFi...");
                    if (mqtt.connected()) {
                      Serial.println("[NET] Tạm dừng MQTT để quét WiFi...");
                      mqtt.disconnect();
                    }
                    last_wifi_recheck = millis();
                    
                    WiFi.mode(WIFI_STA);
                    requestWiFiConnect();
                    
                    unsigned long check_start = millis();
                    bool found_wifi = false;
                    while (millis() - check_start < 15000) {
                        if (WiFi.status() == WL_CONNECTED) {
                            found_wifi = true;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }

                    if (!found_wifi) {
                        Serial.println("[NET] Vẫn không có WiFi, quay lại dùng GSM.");
                        WiFi.mode(WIFI_OFF); 
                        // Cần dừng Client cũ để tránh treo Socket
                        gsmClient.stop(); 
                        // Kiểm tra lại GPRS
                        if (!modem.isGprsConnected()) {
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
        if (current_wifi_ok || (gsm_ok && !is_modem_sleeping)) {
            if (!mqtt.connected()) {
                if (millis() - last_mqtt_retry > 10000 || last_mqtt_retry == 0) {
                    last_mqtt_retry = millis();
                    connectMQTT();
                }
            } 
            
            if (mqtt.connected()) {
                if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    mqtt.loop();
                    xSemaphoreGive(mqttMutex);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

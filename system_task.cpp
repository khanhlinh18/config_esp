#include "system_task.h"

// ================== NVS & CONFIG LOGIC ==================
void updateConfigFromJSON(const char* jsonStr) {
  JsonDocument doc; 
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) return;

  JsonArray slaves = doc["slaves"].as<JsonArray>();

  if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    memset(myGroups, 0, sizeof(myGroups)); 
    int sSize = slaves.size();
    totalGroups = (sSize > MAX_SLAVES) ? MAX_SLAVES : sSize;

    for (int i = 0; i < totalGroups; i++) {
      JsonObject s = slaves[i];
      myGroups[i].id = s["id"] | 1;
      myGroups[i].startReg = s["start"] | 0;
      myGroups[i].count = s["count"] | 0;     
      myGroups[i].dataType = s["dataType"] | 1; 
      myGroups[i].div = s["div"] | 10.0;      

      JsonArray regs = s["regs"].as<JsonArray>();
      int rSize = regs.size();
      myGroups[i].regCount = (rSize > MAX_REGS_PER_SLAVE) ? MAX_REGS_PER_SLAVE : rSize;

      for (int j = 0; j < myGroups[i].regCount; j++) {
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

void loadConfig() {
  prefs.begin("net_cfg", false);
  conf_ssid = prefs.getString("ssid", "");
  conf_pass = prefs.getString("pass", "");
  conf_mqtt_server = prefs.getString("mqtt_srv", "broker.emqx.io");
  conf_mqtt_port = prefs.getInt("mqtt_port", 1883); 
  prefs.end();

  prefs.begin("modbus_cfg", false);
  String savedSlavesJSON = prefs.getString("slave_cfg", "");
  prefs.end();

  if (savedSlavesJSON.length() > 10) {
        Serial.println("[SYSTEM] Tìm thấy cấu hình Slaves, đang khôi phục...");
        updateConfigFromJSON(savedSlavesJSON.c_str());
    } else {
        Serial.println("[SYSTEM] Chưa có cấu hình Slaves trong Flash.");
    }
}

// ================== BUTTON TASK ==================
void taskButton(void *pvParameters) {
  pinMode(SETUP_BUTTON, INPUT_PULLUP);
  uint32_t pressTime = 0;
  bool pressing = false;

  while (1) {
    if (digitalRead(SETUP_BUTTON) == LOW) {
      if (!pressing) { 
        pressTime = millis(); 
        pressing = true; 
      }
      // Nhấn giữ hơn 5 giây
      if (pressing && (millis() - pressTime > 5000)) {
        Serial.println("\n[SYSTEM] ĐANG XÓA CẤU HÌNH VÀ REBOOT...");
        
        prefs.begin("net_cfg", false);
        prefs.clear(); // Xóa sạch SSID/PASS
        prefs.end();

        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
      }
    } else { 
      pressing = false; 
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ================== TASK WATCHDOG ==================
void taskWatchdog(void *pvParameters) {
  esp_task_wdt_add(NULL);
    const uint32_t MAX_OFFLINE_MS = 60 * 60 * 1000UL;      
    const uint32_t MAINTENANCE_REBOOT = 7 * 24 * 60 * 60 * 1000UL; 
    const uint32_t AP_TIMEOUT_MS = 3 * 60 * 1000UL;

    uint32_t last_connected_time = millis();
    uint32_t system_start_time = millis();

    while (1) {
      esp_task_wdt_reset();
        if (mqtt.connected()) last_connected_time = millis();

        if (millis() - last_connected_time > MAX_OFFLINE_MS) {
            Serial.println("[WATCHDOG] Mất kết nối quá lâu. Reboot...");
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }

        if (millis() - system_start_time > MAINTENANCE_REBOOT) {
            Serial.println("[WATCHDOG] Reboot định kỳ...");
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }
        if (setup_mode && (millis() - setup_start_time > AP_TIMEOUT_MS)) {
          Serial.println("[WATCHDOG] Hết thời gian Setup (3 phút). Tắt AP, chuyển qua 4G...");
  
          setup_mode = false;          
          WiFi.softAPdisconnect(true); 
          WiFi.mode(WIFI_STA);   
          digitalWrite(LED_AP, HIGH);      
        }
        for(int i = 0; i < 30; i++) {
            esp_task_wdt_reset(); 
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

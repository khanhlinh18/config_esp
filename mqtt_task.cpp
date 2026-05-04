#include "mqtt_task.h"
#include "system_task.h"

// ================== MQTT LOGIC ==================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_CONFIG) == 0) {
    if (length >= 4096) return;
    static char jsonStr[4096]; 
    memcpy(jsonStr, payload, length);
    jsonStr[length] = '\0';
    Serial.println("[MQTT] Nhận cấu hình mới!");
    updateConfigFromJSON(jsonStr);
  }
}

void connectMQTT() {
  mqtt.setServer(conf_mqtt_server.c_str(), conf_mqtt_port);
  mqtt.setCallback(mqttCallback);
  if (mqtt.connected()) return;
  if (mqtt.connect("esp32_factory_client")) {
    Serial.println("[MQTT] Connected");
    mqtt.subscribe(TOPIC_CONFIG);
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
    if (!setup_mode && mqtt.connected()) {
      String current_ip = "0.0.0.0";
      String current_wifi = "Disconnected";
      int current_rssi = -113;
      bool has_info_changed = false;

      if (WiFi.status() == WL_CONNECTED) {
          current_ip = WiFi.localIP().toString();
          current_wifi = WiFi.SSID();
          current_rssi = WiFi.RSSI();
      } else if (gsm_ok) {
          current_ip = modem.localIP().toString();
          current_wifi = "NO";
          if (!is_modem_sleeping) {
              int rssi_raw = modem.getSignalQuality();
              current_rssi = (rssi_raw != 99) ? (2 * rssi_raw) - 113 : -113;
          } else {
              current_rssi = last_rssi;
          }
      }

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

      if (totalGroups > 0 && (millis() - last_data_send > DATA_INTERVAL)) {
          static char payload[4096];
          int offset = snprintf(payload, sizeof(payload),"{\"data\":[");

          xSemaphoreTake(dataMutex, portMAX_DELAY);
          bool first = true;
          for (int i = 0; i < totalGroups; i++) {
            if (!first) offset += snprintf(payload + offset, sizeof(payload) - offset, ",");

            if (myGroups[i].isLost) {
                offset += snprintf(payload + offset, sizeof(payload) - offset, 
                                   "{\"id\":%d,\"val\":\"NONE\"}", myGroups[i].id);
            } 
            else {
                offset += snprintf(payload + offset, sizeof(payload) - offset, "{\"id\":%d,\"data\":[", myGroups[i].id);
                
                for (int j = 0; j < myGroups[i].regCount; j++) {
                    if (j > 0) offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
                    
                    int regAddr = myGroups[i].startReg + (myGroups[i].dataType == 2 ? j*2 : j);
                    offset += snprintf(payload + offset, sizeof(payload) - offset, 
                                       "{\"reg\":%d,\"val\":%.1f}", regAddr, myGroups[i].lastData[j]);
                }
                offset += snprintf(payload + offset, sizeof(payload) - offset, "]}");
            }
            first = false;
          }
          xSemaphoreGive(dataMutex);

          snprintf(payload + offset, sizeof(payload) - offset, "]}");
          
          if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
            if (mqtt.publish(TOPIC_DATA, payload)) {
                Serial.println("[MQTT] Modbus Data Sent");
                last_data_send = millis();
            }
            xSemaphoreGive(mqttMutex);
          }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

#include "alarm_task.h"

// ================== ALARM BUZZER ==================
void taskBuzzer(void *pvParameters){
  while (1) {
    bool sensor, net, mqtt_err;

    xSemaphoreTake(alarmMutex, portMAX_DELAY);
    sensor = alarm_sensor;
    net = alarm_network;
    mqtt_err = alarm_mqtt;
    xSemaphoreGive(alarmMutex);

    if (sensor || net || mqtt_err) {
      digitalWrite(RELAY_ALARM, LOW);
      vTaskDelay(pdMS_TO_TICKS(1000));
      digitalWrite(RELAY_ALARM, HIGH);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }else {
      digitalWrite(RELAY_ALARM, HIGH);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

// ================== ALARM TASK ==================
void taskAlarm(void *pvParameters) {
  unsigned long last_send_time = 0;
  const uint32_t ALARM_INTERVAL = 5000; 
  bool last_anySensorAlarm = false; 

  while (1) {
    bool anySensorAlarm = false;
    char alarmList[1024] = ""; 
    int offset = 0; 

    if (!setup_mode && totalGroups > 0) {
      xSemaphoreTake(configMutex, portMAX_DELAY);
      xSemaphoreTake(dataMutex, portMAX_DELAY); 

    for (int i = 0; i < totalGroups; i++) {
        uint8_t id = myGroups[i].id;
        bool is_id_lost = false;
        char alarmMsg[128] = "";

      if (myGroups[i].isLost) {
        is_id_lost = true;
        anySensorAlarm = true;
        snprintf(alarmMsg, sizeof(alarmMsg), "[ID%d:LOST] ", id);
        
        for(int j=0; j < myGroups[i].regCount; j++) myGroups[i].alarmState[j] = true;
      } 
    
      if (!is_id_lost) {
        for (int j = 0; j < myGroups[i].regCount; j++) {
            float val = myGroups[i].lastData[j];
            bool currentItemAlarm = false;
            char itemMsg[64] = "";

            if (myGroups[i].regs[j].type == 0) {
                if (val > myGroups[i].regs[j].maxVal) {
                    currentItemAlarm = true;
                    snprintf(itemMsg, sizeof(itemMsg), "[ID%d:HI_%s] ", id, myGroups[i].regs[j].err);
                } else if (val < myGroups[i].regs[j].minVal) {
                    currentItemAlarm = true;
                    snprintf(itemMsg, sizeof(itemMsg), "[ID%d:LO_%s] ", id, myGroups[i].regs[j].err);
                }
            } else if (myGroups[i].regs[j].type == 1) {
                if (val < myGroups[i].regs[j].minVal) {
                    currentItemAlarm = true;
                    snprintf(itemMsg, sizeof(itemMsg), "[ID%d:%s] ", id, myGroups[i].regs[j].err);
                }
            }

            myGroups[i].alarmState[j] = currentItemAlarm;
            if (currentItemAlarm) {
                anySensorAlarm = true;
                strncat(alarmMsg, itemMsg, sizeof(alarmMsg) - strlen(alarmMsg) - 1);
            }
        }
      }

      if (strlen(alarmMsg) > 0 && offset + strlen(alarmMsg) < sizeof(alarmList)) {
        strcat(alarmList, alarmMsg);
        offset += strlen(alarmMsg);
      }
    } 
      xSemaphoreGive(configMutex);
      xSemaphoreGive(dataMutex);
    }

    if (mqtt.connected()) {
      char publishPayload[1200] = "";
      bool shouldPublish = false;

      if (anySensorAlarm != last_anySensorAlarm) { 
        if (anySensorAlarm) {
          snprintf(publishPayload, sizeof(publishPayload), "START ALARM: %s", alarmList);
        } else {
          snprintf(publishPayload, sizeof(publishPayload), "NORMAL");
        }
        shouldPublish = true;
      } 
      else if (millis() - last_send_time > ALARM_INTERVAL) {
        if (anySensorAlarm) {
          snprintf(publishPayload, sizeof(publishPayload), "STILL ALARM: %s", alarmList);
        } else {
          snprintf(publishPayload, sizeof(publishPayload), "NORMAL");
        }
        shouldPublish = true;
      }

      if (shouldPublish) {
        if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
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

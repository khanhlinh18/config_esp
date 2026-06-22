#include "modbus_task.h"

// ================== RS485 CONTROL ==================
void preTransmission()  { digitalWrite(MAX485_DE, 1); }
void postTransmission() { digitalWrite(MAX485_DE, 0); }

// ================== RELAY RS485 ==================
void writeRelayRS485(bool state) {
    if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    node.begin(RELAY_MODBUS_ID, MODBUS_SERIAL);
    preTransmission();
    uint8_t result = node.writeSingleCoil(RELAY_COIL_ADDR, state ? 0xFF00 : 0x0000);
    postTransmission();
    xSemaphoreGive(modbusMutex);
    if (result != node.ku8MBSuccess)
        LOG("ALARM", "Ghi relay RS485 thất bại, mã lỗi: %d", result);
}

// ================== ALARM TASK ==================
void taskAlarm(void* pvParameters) {
    bool lastRelayState = false;
    while (1) {
        if (alarm_cloud) {
            writeRelayRS485(true);
            lastRelayState = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            writeRelayRS485(false);
            lastRelayState = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            if (lastRelayState) { writeRelayRS485(false); lastRelayState = false; }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// ================== CONFIG FROM JSON ==================
void updateConfigFromJSON(const char* jsonStr) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) return;
    JsonArray slaves = doc["slaves"].as<JsonArray>();
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        memset(myGroups, 0, sizeof(myGroups));
        int sSize = slaves.size();
        totalGroups = (sSize > MAX_SLAVES) ? MAX_SLAVES : sSize;
        for (int i = 0; i < totalGroups; i++) {
            JsonObject s = slaves[i];
            myGroups[i].id       = s["id"]      | 1;
            myGroups[i].startReg = s["start"]    | 0;
            myGroups[i].count    = s["count"]    | 0;
            myGroups[i].dataType = s["dataType"] | 1;
            myGroups[i].div      = s["div"]      | 10.0;
            if (myGroups[i].count > MAX_SLAVES) myGroups[i].count = MAX_SLAVES;
            for (int j = 0; j < myGroups[i].count; j++) myGroups[i].lastData[j] = -9999.0;
        }
        xSemaphoreGive(configMutex);
        prefs.begin("modbus_cfg", false);
        prefs.putString("slave_cfg", jsonStr);
        prefs.end();
    }
}

// ================== MODBUS TASK ==================
void taskModbus(void* pvParameters) {
    while (1) {
        if (!setup_mode && totalGroups > 0) {
            for (int i = 0; i < totalGroups; i++) {
                xSemaphoreTake(configMutex, portMAX_DELAY);
                uint8_t  id      = myGroups[i].id;
                uint16_t start   = myGroups[i].startReg;
                uint8_t  qty     = myGroups[i].count;
                uint8_t  dType   = myGroups[i].dataType;
                float    divisor = myGroups[i].div;
                xSemaphoreGive(configMutex);

                if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(1000)) != pdTRUE) { vTaskDelay(pdMS_TO_TICKS(150)); continue; }
                node.begin(id, MODBUS_SERIAL);
                uint8_t result = node.readHoldingRegisters(start, qty);
                xSemaphoreGive(modbusMutex);

                xSemaphoreTake(dataMutex, portMAX_DELAY);
                if (result == node.ku8MBSuccess) {
                    if (myGroups[i].isLost && myGroups[i].hasBeenRead) {
                        char _buf[40]; snprintf(_buf, sizeof(_buf), "Slave ID%d recovered", myGroups[i].id);
                        sdLog("INFO", "MODBUS", _buf);
                    }
                    myGroups[i].isLost = false; myGroups[i].hasBeenRead = true;
                    int bufferOffset = 0;
                    for (int j = 0; j < myGroups[i].count && j < MAX_SLAVES; j++) {
                        float rawVal = 0;
                        if (dType == 2) {
                            if (bufferOffset + 1 >= qty) break;
                            uint32_t data = ((uint32_t)node.getResponseBuffer(bufferOffset) << 16) | node.getResponseBuffer(bufferOffset + 1);
                            rawVal = (float)data; bufferOffset += 2;
                        } else { rawVal = (float)node.getResponseBuffer(bufferOffset); bufferOffset += 1; }
                        myGroups[i].lastData[j] = rawVal / divisor;
                    }
                } else {
                    if (!myGroups[i].isLost && myGroups[i].hasBeenRead) {
                        char _buf[40]; snprintf(_buf, sizeof(_buf), "Slave ID%d LOST", myGroups[i].id);
                        sdLog("WARN", "MODBUS", _buf);
                    }
                    myGroups[i].isLost = true;
                    for (int j = 0; j < myGroups[i].count && j < MAX_SLAVES; j++) myGroups[i].lastData[j] = -9999.0;
                }
                xSemaphoreGive(dataMutex);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

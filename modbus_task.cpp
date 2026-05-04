#include "modbus_task.h"

// ================== RS485 ==================
void preTransmission() { digitalWrite(MAX485_DE, 1); }
void postTransmission() { digitalWrite(MAX485_DE, 0); }

// ================== TASK MODBUS  ==================
void taskModbus(void *pvParameters) {
  while (1) {
    if (!setup_mode && totalGroups > 0) {
      for (int i = 0; i < totalGroups; i++) {
        xSemaphoreTake(configMutex, portMAX_DELAY);
        uint8_t id = myGroups[i].id;
        uint16_t start = myGroups[i].startReg;
        uint8_t qty = myGroups[i].count;
        uint8_t dType = myGroups[i].dataType;
        float divisor = myGroups[i].div;
        //uint8_t rCount = myGroups[i].regCount;
        xSemaphoreGive(configMutex);

        node.begin(id, MODBUS_SERIAL);
        uint8_t result = node.readHoldingRegisters(start, qty); 

        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (result == node.ku8MBSuccess) {
          myGroups[i].isLost = false;

          int bufferOffset = 0;
          for (int j = 0; j < myGroups[i].regCount; j++) {
          float rawVal = 0;

          if (dType == 2) {
          if (bufferOffset + 1 >= qty) break;
          uint32_t data = ((uint32_t)node.getResponseBuffer(bufferOffset) << 16) |
                         node.getResponseBuffer(bufferOffset + 1);
          rawVal = (float)data;
          bufferOffset += 2;
        } else {
          rawVal = (float)node.getResponseBuffer(bufferOffset);
          bufferOffset += 1;
        }

        myGroups[i].lastData[j] = rawVal / divisor;
          }
        } else {
            myGroups[i].isLost = true;
            
            for (int j = 0; j < myGroups[i].regCount; j++) {
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

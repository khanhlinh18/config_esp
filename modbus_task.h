#ifndef MODBUS_TASK_H
#define MODBUS_TASK_H

#include "globals.h"

void preTransmission();
void postTransmission();
void taskModbus(void *pvParameters);

#endif

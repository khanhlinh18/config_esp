#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include "globals.h"

void updateConfigFromJSON(const char* jsonStr);
void loadConfig();
void taskWatchdog(void *pvParameters);
void taskButton(void *pvParameters);

#endif

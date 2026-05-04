#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#include "globals.h"

void requestWiFiConnect();
void resetModem();
bool connectGSM();
void getStaticSimInfo();
void taskNetwork(void *pvParameters);

#endif

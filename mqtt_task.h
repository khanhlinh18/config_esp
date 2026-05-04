#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include "globals.h"

void mqttCallback(char* topic, byte* payload, unsigned int length);
void connectMQTT();
void taskMQTTPublish(void *pvParameters);

#endif

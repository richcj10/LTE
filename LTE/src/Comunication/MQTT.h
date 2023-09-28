#ifndef MQTT_H
#define  MQTT_H

#define PO 22

void MqttLoop(void);
void MQTTreconnect(void);
void MQTTStart(void);
void SendChestFreezer(void);
void SendDeviceEnviroment(void);
void SendChestPower(char Mode);
void MQTTMessageUpdate(void);
char GetMQTTStatus(void);
char GetMQTTMsg();
void CLRMQTTMsg();

#endif  /* OLED_H */

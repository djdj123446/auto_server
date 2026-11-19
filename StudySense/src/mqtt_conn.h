#ifndef MQTT_CONN_H
#define MQTT_CONN_H

#include <Arduino.h>

// ======================================================
// 自建云平台 MQTT 对接（EMQX Broker）
// ======================================================

// 是否已连接云平台
bool mqttConnected();

// 初始化 MQTT 客户端（设置服务器、回调）
void mqttBegin();

// 保持连接并处理收发消息（需在 loop 中调用）
void mqttLoop();

// 上报传感器数据到云平台（主题 state/up）
void mqttReportAttributes();

// 上报阈值到云平台（主题 threshold/up，设备端改阈值后调用）
void mqttReportThresholds();

#endif

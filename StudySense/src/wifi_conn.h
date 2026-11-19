#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#include <Arduino.h>

// ======================================================
// 函数
// ======================================================

// 开始连接 WiFi（非阻塞）
void wifiBegin();

// 是否已连接
bool wifiConnected();

// 获取 IP 地址
String wifiGetIP();

#endif

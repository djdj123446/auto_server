#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "wifi_conn.h"

// ======================================================
// 连接 WiFi
// ======================================================

void wifiBegin()
{
    WiFi.mode(WIFI_STA);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("正在连接WiFi：");
    Serial.println(WIFI_SSID);
}

// ======================================================
// 连接状态
// ======================================================

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

// ======================================================
// IP 地址
// ======================================================

String wifiGetIP()
{
    return WiFi.localIP().toString();
}

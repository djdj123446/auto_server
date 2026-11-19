#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>

#include "config.h"
#include "sensors.h"

// ======================================================
// 传感器对象
// ======================================================

DHT dht(DHT_PIN, DHT_TYPE);

BH1750 lightMeter;

// ======================================================
// 传感器数据
// ======================================================

float temperature = 0.0;
float humidity = 0.0;
float lux = 0.0;

bool peopleDetected = false;

// ======================================================
// 初始化
// ======================================================

void sensorsInit()
{
    // ==================================================
    // DHT11
    // ==================================================

    dht.begin();

    // ==================================================
    // BH1750
    // ==================================================

    if (lightMeter.begin())
    {
        Serial.println("BH1750 初始化成功");
    }
    else
    {
        Serial.println("BH1750 初始化失败！");
    }

    // ==================================================
    // HC-SR501
    // ==================================================

    pinMode(PIR_PIN, INPUT);
}

// ======================================================
// 读取传感器
// ======================================================

void readSensors()
{
    // 温度
    float newTemperature = dht.readTemperature();

    if (!isnan(newTemperature))
    {
        temperature = newTemperature;
    }

    // 湿度
    float newHumidity = dht.readHumidity();

    if (!isnan(newHumidity))
    {
        humidity = newHumidity;
    }

    // 光照
    float newLux = lightMeter.readLightLevel();

    if (newLux >= 0)
    {
        lux = newLux;
    }

    // 人体感应
    peopleDetected = (digitalRead(PIR_PIN) == HIGH);
}

#ifndef SENSORS_H
#define SENSORS_H

// ======================================================
// 传感器数据
// ======================================================

extern float temperature;   // 温度 C
extern float humidity;      // 湿度 %
extern float lux;           // 光照 lx
extern bool peopleDetected; // 人体感应

// ======================================================
// 函数
// ======================================================

// 初始化所有传感器（DHT11、BH1750、HC-SR501）
void sensorsInit();

// 读取所有传感器并更新数据
void readSensors();

#endif

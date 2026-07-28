// SHTC3 温湿度 + PCF85063 RTC,均挂在 Wire (SDA=13, SCL=14)
#pragma once

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

void Sensors_Init();

// 读温湿度,成功返回 true。测量周期 ~25ms,读完传感器回到休眠。
bool Shtc3_Read(float *tempC, float *humi);

// PCF85063:读/写日历时间(本地时间)。读到的年份 < 2024 视为时间无效。
bool Rtc_GetTime(struct tm *out);
bool Rtc_SetTime(const struct tm *t);
